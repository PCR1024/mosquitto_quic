#ifdef WITH_QUIC
#include "quic_mosq.h"
#include "logging_mosq.h"
#include "memory_mosq.h"


const QUIC_API_TABLE* msquic = NULL;
HQUIC registration;
HQUIC configuration;

int msquic_init(void) 
{
    if (msquic == NULL) {
        QUIC_STATUS status = QUIC_STATUS_SUCCESS;
        if (QUIC_FAILED(status = MsQuicOpen2(&msquic))) {
            log__printf(NULL, MOSQ_LOG_ERR, "Error: MsQuicOpen2 failed, 0x%x!", status);
            return MOSQ_ERR_QUIC;
        }
    }
    return MOSQ_ERR_SUCCESS;
}

void msquic_cleanup(void) 
{
    if (msquic != NULL) {
        MsQuicClose(msquic);
        msquic = NULL;
    }
}

int msquic_config(struct mosquitto *mosq)
{
    if(msquic == NULL) {
        return MOSQ_ERR_QUIC;
    }

    QUIC_STATUS status = QUIC_STATUS_SUCCESS;

    // 1. 注册配置
    const QUIC_REGISTRATION_CONFIG regconfig = {
        "mosquitto", 
        mosq->quic_execution_profile
    };
    if (QUIC_FAILED(status = msquic->RegistrationOpen(
            &regconfig, 
            &registration))) {
        log__printf(mosq, MOSQ_LOG_ERR, "Error: RegistrationOpen failed, 0x%x!", status);
        goto Error;
    }

    // 2. ALPN和idle设置
    const QUIC_BUFFER alpn = { 
        sizeof("mqtt") - 1, 
        (uint8_t*)"mqtt" 
    };
    QUIC_SETTINGS settings = {0};
    settings.IdleTimeoutMs = 0;
    settings.IsSet.IdleTimeoutMs = 1;

    if (QUIC_FAILED(status = msquic->ConfigurationOpen(
            registration,
            &alpn,
            1,
            &settings,
            sizeof(settings),
            NULL,
            &configuration))) {
        log__printf(mosq, MOSQ_LOG_ERR, "Error: ConfigurationOpen failed, 0x%x!", status);
        goto Error;
    }

    // 3. 证书配置
    QUIC_CREDENTIAL_CONFIG credconfig;
    memset(&credconfig, 0, sizeof(credconfig));
    credconfig.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credconfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | 
                           QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    
    if (QUIC_FAILED(status = msquic->ConfigurationLoadCredential(
            configuration,
            &credconfig))) {
        log__printf(mosq, MOSQ_LOG_ERR, "Error: ConfigurationLoadCredential failed, 0x%x!", status);
        goto Error;
    }

    return MOSQ_ERR_SUCCESS;

Error:
    if (msquic != NULL) {
        if (configuration != NULL) {
            msquic->ConfigurationClose(configuration);
            configuration = NULL;
        }
        if (registration != NULL) {
            msquic->RegistrationClose(registration);
            registration = NULL;
        }
    }
    return MOSQ_ERR_QUIC;
}

static void handle_quic_state_transition(
    struct mosquitto *mosq, 
    enum mosquitto_quic_state new_state) 
{
    pthread_mutex_lock(&mosq->quic_session->state_mutex);
    
    enum mosquitto_quic_state old_state = mosq->quic_session->state;
    mosq->quic_session->state = new_state;
    
    if (old_state == mosq_qs_connecting || 
        (old_state == mosq_qs_connected && new_state == mosq_qs_closed)) {
        pthread_cond_broadcast(&mosq->quic_session->state_cond);
    }
    
    pthread_mutex_unlock(&mosq->quic_session->state_mutex);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
quic_client_connection_callback(
    _In_ HQUIC connection,
    _In_opt_ void* context,
    _Inout_ QUIC_CONNECTION_EVENT* event
    )
{
    struct mosquitto *mosq = (struct mosquitto *)context;
    switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] Connected", connection);
        handle_quic_state_transition(mosq, mosq_qs_connected);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        if (event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
            log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] Successfully shut down on idle.", connection);
        } else {
            log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] Shut down by transport, 0x%x", connection, event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        }
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] Shut down by peer, 0x%llu", connection, (unsigned long long)event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] All done", connection);
        if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
            msquic->ConnectionClose(connection);
        }
        if (mosq->quic_session->state == mosq_qs_connecting) {
            handle_quic_state_transition(mosq, mosq_qs_failed);
        } else if (mosq->quic_session->state == mosq_qs_connected) {
            handle_quic_state_transition(mosq, mosq_qs_closed);
        }
        break;
    case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

static int msquic_wait_connection(struct mosquitto *mosq) 
{
    int rc;

    pthread_mutex_lock(&mosq->quic_session->state_mutex);
    
    while (mosq->quic_session->state == mosq_qs_connecting) {
        rc = pthread_cond_wait(&mosq->quic_session->state_cond, 
                              &mosq->quic_session->state_mutex);
        if (rc) {
            pthread_mutex_unlock(&mosq->quic_session->state_mutex);
            return MOSQ_ERR_UNKNOWN;
        }
    }
    
    switch (mosq->quic_session->state) {
        case mosq_qs_connected:
            rc = MOSQ_ERR_SUCCESS;
            break;
        case mosq_qs_failed:
            rc = MOSQ_ERR_QUIC_HANDSHAKE;
            break;
        default:
            rc = MOSQ_ERR_UNKNOWN;
    }
    
    pthread_mutex_unlock(&mosq->quic_session->state_mutex);
    return rc;
}

int msquic_try_connect(struct mosquitto *mosq, const char *host, uint16_t port, const char *bind_address)
{
    if (!mosq || !mosq->quic_session) {
        return MOSQ_ERR_INVAL;
    }

    if(msquic == NULL) {
        return MOSQ_ERR_QUIC;
    }
     QUIC_STATUS status = QUIC_STATUS_SUCCESS;

     mosq->quic_session->state = mosq_qs_connecting;
    
     status = msquic->ConnectionOpen(
        registration,
        quic_client_connection_callback,
        mosq,
        &(mosq->quic_session->connection));
    
    if (QUIC_FAILED(status)) {
        log__printf(mosq, MOSQ_LOG_ERR, "Error: ConnectionOpen failed, 0x%x!", status);
        goto Error;
    }

    /* 设置本地地址（如果提供） */
    if (bind_address) {
        status = msquic->SetParam(
            mosq->quic_session->connection,
            QUIC_PARAM_CONN_LOCAL_ADDRESS,
            sizeof(bind_address),
            bind_address);
        
        if (QUIC_FAILED(status)) {
            log__printf(mosq, MOSQ_LOG_WARNING,"Warning: Unable to set local address, 0x%x!", status);
        }
    }

    status = msquic->ConnectionStart(
        mosq->quic_session->connection,
        configuration,
        QUIC_ADDRESS_FAMILY_UNSPEC,
        host,
        port);

    if (QUIC_FAILED(status)) {
        log__printf(mosq, MOSQ_LOG_ERR, "Error: ConnectionStart failed, 0x%x!", status);
        goto Error;
    }

    return msquic_wait_connection(mosq);

Error:
    if (mosq->quic_session->connection != NULL) {
        msquic->ConnectionClose(mosq->quic_session->connection);
    }
    return MOSQ_ERR_QUIC;
}

static int msquic_wait_close(struct mosquitto *mosq) 
{
    int rc;

    pthread_mutex_lock(&mosq->quic_session->state_mutex);
    
    while (mosq->quic_session->state == mosq_qs_connected) {
        rc = pthread_cond_wait(&mosq->quic_session->state_cond, 
                              &mosq->quic_session->state_mutex);
        if (rc) {
            pthread_mutex_unlock(&mosq->quic_session->state_mutex);
            return MOSQ_ERR_UNKNOWN;
        }
    }
    
    rc = (mosq->quic_session->state == mosq_qs_closed) ? 
         MOSQ_ERR_SUCCESS : MOSQ_ERR_UNKNOWN;
    
    pthread_mutex_unlock(&mosq->quic_session->state_mutex);
    return rc;
}

int msquic_try_close(struct mosquitto *mosq)
{
    if (!mosq || !mosq->quic_session) {
        return MOSQ_ERR_INVAL;
    }

    if(msquic == NULL) {
        return MOSQ_ERR_QUIC;
    }

    if (mosq->quic_session->state == mosq_qs_connected) {
        msquic->ConnectionShutdown(
            mosq->quic_session->connection, 
            QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 
            0);
    }
    return msquic_wait_close(mosq);
}

ssize_t msquic_send(struct mosquitto *mosq, const void *buf, size_t count)
{
    uint8_t* SendBufferRaw;
    QUIC_BUFFER* SendBuffer;

    SendBufferRaw = (uint8_t*)malloc(sizeof(QUIC_BUFFER) + count);
    if (SendBufferRaw == NULL) {
        return -1;
    }
    SendBuffer = (QUIC_BUFFER*)SendBufferRaw;
    SendBuffer->Buffer = SendBufferRaw + sizeof(QUIC_BUFFER);
    SendBuffer->Length = count;
    memcpy(SendBuffer->Buffer, buf, count);

    QUIC_STATUS Status;
    if (QUIC_FAILED(Status = msquic->StreamSend(mosq->quic_session->stream, SendBuffer, 1, QUIC_SEND_FLAG_NONE, SendBuffer))) {
        printf("StreamSend failed, 0x%x!\n", Status);
        free(SendBufferRaw);
        return -1;
    }

    ssize_t bytesSent = (ssize_t)count;

    return bytesSent;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
QuicClientStreamCallback(
    _In_ HQUIC stream,
    _In_opt_ void* context,
    _Inout_ QUIC_STREAM_EVENT* event
    )
{
    struct mosquitto *mosq = (struct mosquitto *)context;
    switch (event->Type) {
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        //
        // A previous StreamSend call has completed, and the context is being
        // returned back to the app.
        //
        free(event->SEND_COMPLETE.ClientContext);
        log__printf(mosq, MOSQ_LOG_DEBUG, "[strm][%p] Data sent\n", stream);
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
        //
        // Data was received from the peer on the stream.
        //
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        //
        // The peer aborted its send direction of the stream.
        //
        log__printf(mosq, MOSQ_LOG_DEBUG, "[strm][%p] Peer shut down\n", stream);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
        //
        // Both directions of the stream have been shut down and MsQuic is done
        // with the stream. It can now be safely cleaned up.
        //
        log__printf(mosq, MOSQ_LOG_DEBUG, "[strm][%p] All done\n", stream);
        if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
            msquic->StreamClose(stream);
        }
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

#endif
