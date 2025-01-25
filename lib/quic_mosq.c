#ifdef WITH_QUIC
#include "logging_mosq.h"
#include "memory_mosq.h"
#include "packet_mosq.h"
#include "quic_mosq.h"

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

static void connection_state_transition(
    struct mosq_quic_connection *connection, 
    enum mosquitto_quic_connection_state new_state) 
{
    pthread_mutex_lock(&connection->state_mutex);
    
    enum mosquitto_quic_connection_state old_state = connection->state;
    connection->state = new_state;
    
    if (old_state == mosq_qs_connecting || 
        (old_state == mosq_qs_connected && new_state == mosq_qs_closed)) {
        pthread_cond_broadcast(&connection->state_cond);
    }
    
    pthread_mutex_unlock(&connection->state_mutex);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS
QUIC_API
quic_client_stream_callback(
    _In_ HQUIC stream,
    _In_opt_ void* context,
    _Inout_ QUIC_STREAM_EVENT* event
    )
{
    struct mosquitto *mosq = (struct mosquitto *)context;
    switch (event->Type) {
    case QUIC_STREAM_EVENT_SEND_COMPLETE:
        free(event->SEND_COMPLETE.ClientContext);
        break;
    case QUIC_STREAM_EVENT_RECEIVE:
        mosq->stream.packet_reader.buffers = event->RECEIVE.Buffers;
        mosq->stream.packet_reader.buffer_count = event->RECEIVE.BufferCount;
        mosq->stream.packet_reader.current_buffer = 0;
        mosq->stream.packet_reader.buffer_pos = 0;
        mosq->stream.packet_reader.total_length = event->RECEIVE.TotalBufferLength;
        mosq->stream.packet_reader.consumed_length = 0;
        mosquitto_loop_read(mosq, 1);
        if(mosq->stream.packet_reader.consumed_length < mosq->stream.packet_reader.total_length) {
            msquic->StreamReceiveSetEnabled(stream, TRUE);
        }
        event->RECEIVE.TotalBufferLength = mosq->stream.packet_reader.consumed_length;
        break;
    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        log__printf(mosq, MOSQ_LOG_DEBUG, "[strm][%p] Peer shut down\n", stream);
        break;
    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
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

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS
QUIC_API
quic_client_connection_callback(
    _In_ HQUIC handle,
    _In_opt_ void* context,
    _Inout_ QUIC_CONNECTION_EVENT* event
    )
{
    struct mosquitto *mosq = (struct mosquitto *)context;
    struct mosq_quic_connection *connection = &mosq->connection;

    switch (event->Type) {
    case QUIC_CONNECTION_EVENT_CONNECTED:
        log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] Connected", handle);
        connection_state_transition(connection, mosq_qs_connected);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        if (event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status == QUIC_STATUS_CONNECTION_IDLE) {
            log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] Successfully shut down on idle.", handle);
        } else {
            log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] Shut down by transport, 0x%x", handle, event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status);
        }
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] Shut down by peer, 0x%llu", handle, (unsigned long long)event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode);
        break;
    case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        log__printf(mosq, MOSQ_LOG_DEBUG, "[conn][%p] All done", handle);
        if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) {
            msquic->ConnectionClose(handle);
        }
        if (connection->state == mosq_qs_connecting) {
            connection_state_transition(connection, mosq_qs_failed);
        } else if (connection->state == mosq_qs_connected) {
            connection_state_transition(connection, mosq_qs_closed);
        }
        break;
    case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
        break;
    default:
        break;
    }
    return QUIC_STATUS_SUCCESS;
}

static int msquic_wait_connection(struct mosq_quic_connection *connection) 
{
    int rc;

    pthread_mutex_lock(&connection->state_mutex);
    
    while (connection->state == mosq_qs_connecting) {
        rc = pthread_cond_wait(&connection->state_cond, 
                              &connection->state_mutex);
        if (rc) {
            pthread_mutex_unlock(&connection->state_mutex);
            return MOSQ_ERR_UNKNOWN;
        }
    }
    
    switch (connection->state) {
        case mosq_qs_connected:
            rc = MOSQ_ERR_SUCCESS;
            break;
        case mosq_qs_failed:
            rc = MOSQ_ERR_QUIC_HANDSHAKE;
            break;
        default:
            rc = MOSQ_ERR_UNKNOWN;
    }
    pthread_mutex_unlock(&connection->state_mutex);
    return rc;
}

int msquic_try_connect(struct mosquitto *mosq, const char *host, uint16_t port, const char *bind_address)
{
    if(!msquic) {
        log__printf(mosq, MOSQ_LOG_ERR, "Error: msquic is NULL!");
        return MOSQ_ERR_QUIC;
    }

    struct mosq_quic_connection *connection = &mosq->connection;
    connection->state = mosq_qs_connecting;
    
    QUIC_STATUS status = QUIC_STATUS_SUCCESS;
    
    status = msquic->ConnectionOpen(
        registration,
        quic_client_connection_callback,
        mosq,
        &connection->handle);
    
    if (QUIC_FAILED(status)) {
        log__printf(mosq, MOSQ_LOG_ERR, "Error: ConnectionOpen failed, 0x%x!", status);
        goto Error;
    }

    if (bind_address) {
        status = msquic->SetParam(
            connection->handle,
            QUIC_PARAM_CONN_LOCAL_ADDRESS,
            sizeof(bind_address),
            bind_address);
        if (QUIC_FAILED(status)) {
            log__printf(mosq, MOSQ_LOG_WARNING,"Warning: Unable to set local address, 0x%x!", status);
        }
    }

    status = msquic->ConnectionStart(
        connection->handle,
        configuration,
        QUIC_ADDRESS_FAMILY_UNSPEC,
        host,
        port);

    if (QUIC_FAILED(status)) {
        log__printf(mosq, MOSQ_LOG_ERR, "Error: ConnectionStart failed, 0x%x!", status);
        goto Error;
    }

    int rc = msquic_wait_connection(connection);
    if (!rc)
    {
        if (QUIC_FAILED(status = msquic->StreamOpen(connection->handle, QUIC_STREAM_OPEN_FLAG_NONE, quic_client_stream_callback, mosq, &mosq->stream.handle))) {
            log__printf(mosq, MOSQ_LOG_ERR, "StreamOpen failed, 0x%x!", status);
        }
        if (QUIC_FAILED(status = msquic->StreamStart(mosq->stream.handle, QUIC_STREAM_START_FLAG_NONE))) {
            log__printf(mosq, MOSQ_LOG_ERR, "StreamStart failed, 0x%x!", status);
            msquic->StreamClose(mosq->stream.handle);
        }
    }
    
    return rc;
    
Error:
    if (connection->handle != NULL) {
        msquic->ConnectionClose(connection->handle);
        connection->handle = NULL;
    }
    return MOSQ_ERR_QUIC;
}

static int msquic_wait_close(struct mosq_quic_connection *connection) 
{
    int rc;

    pthread_mutex_lock(&connection->state_mutex);
    
    while (connection->state == mosq_qs_connected) {
        rc = pthread_cond_wait(&connection->state_cond, 
                              &connection->state_mutex);
        if (rc) {
            pthread_mutex_unlock(&connection->state_mutex);
            return MOSQ_ERR_UNKNOWN;
        }
    }
    
    rc = (connection->state == mosq_qs_closed) ? 
         MOSQ_ERR_SUCCESS : MOSQ_ERR_UNKNOWN;
    
    pthread_mutex_unlock(&connection->state_mutex);

    return rc;
}

int msquic_try_close(struct mosq_quic_connection *connection)
{
    if(!msquic) {
        return MOSQ_ERR_QUIC;
    }

    if(connection->handle)
    {
        msquic->ConnectionShutdown(
            connection->handle, 
            QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 
            0);
        return msquic_wait_close(connection);
    }
    return MOSQ_ERR_SUCCESS;
}

ssize_t msquic_send(HQUIC stream, const void *buf, size_t count)
{
    if (!stream || !buf) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }

    if (count > UINT32_MAX) {
        return -1; 
    }

    const size_t total_size = sizeof(QUIC_BUFFER) + count;
    uint8_t *mem = (uint8_t *)malloc(total_size);
    if (!mem) {
        return -1;
    }

    QUIC_BUFFER *qbuf = (QUIC_BUFFER *)mem;
    qbuf->Buffer = mem + sizeof(QUIC_BUFFER);
    qbuf->Length = (uint32_t)count;

    memcpy(qbuf->Buffer, buf, count);
    
    QUIC_STATUS status = msquic->StreamSend(
        stream,
        qbuf,
        1,
        QUIC_SEND_FLAG_NONE,
        qbuf
    );
    if (QUIC_FAILED(status)) {
        free(mem);
        return -1;
    }

    return (ssize_t)count;
}


ssize_t msquic_recv(struct mosq_quic_packet_reader* reader, void *buf, size_t count)
{
    if (!reader || !buf) {
        return -1;
    }

    if (count == 0) {
        return 0;
    }
    
    if (reader->consumed_length == reader->total_length) {
        return 0;
    }

    
    const uint64_t total_remaining = reader->total_length - reader->consumed_length;
    
    if (count > total_remaining) {
        count = (size_t)total_remaining;
    }

    size_t bytes_read = 0;
    uint8_t *out_ptr = (uint8_t *)buf;

    while (bytes_read < count && reader->current_buffer < reader->buffer_count)
    {
        const QUIC_BUFFER *cur_buf = &reader->buffers[reader->current_buffer];
        
        const size_t buffer_remaining = cur_buf->Length - reader->buffer_pos;
        if (buffer_remaining == 0) {
            reader->current_buffer++;
            reader->buffer_pos = 0;
            continue;
        }
        
        const size_t need = count - bytes_read;
        size_t to_read = (need < buffer_remaining) ? need : buffer_remaining;

        memcpy(out_ptr + bytes_read, cur_buf->Buffer + reader->buffer_pos, to_read);

        bytes_read += to_read;
        reader->buffer_pos += (uint32_t)to_read;
        reader->consumed_length += to_read;
    }

    return (ssize_t)bytes_read;
}
#endif
