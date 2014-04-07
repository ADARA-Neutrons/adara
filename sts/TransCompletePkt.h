#ifndef TRANSCOMPLETEPKT_H
#define TRANSCOMPLETEPKT_H

#include <string>

namespace STS {

/**
 * @brief The TransCompletePkt class wraps the translation status message sent back to the SMS.
 *
 * The status code and optional 'reason' message (for errors) is packaged into an ADARA
 * message in an internal buffer.
 */
class TransCompletePkt
{
public:
    /// Deafult constructor
    TransCompletePkt()
      : m_buf_ready(false), m_buffer(0), m_buffer_len(0), m_buffer_capacity(0), m_status(0)
    {}

    /// Constructor taking status and reason parameters
    TransCompletePkt( uint16_t a_status, const std::string &a_reason )
      : m_buf_ready(false), m_buffer(0), m_buffer_len(0), m_buffer_capacity(0), m_status(a_status), m_reason(a_reason)
    {}

    /// Destructor
    ~TransCompletePkt()
    {
        if ( m_buffer )
            delete[] m_buffer;
    }

    /// Sets status code and reason message
    void
    set( uint16_t a_status, const std::string &a_reason )
    {
        m_status = a_status;
        m_reason = a_reason;
        m_buf_ready = false;
    }

    /// Retrieves buffer containing ADARA message
    const char *
    getMessageBuffer()
    {
        if ( !m_buf_ready )
            buildSendBuffer();

        return m_buffer;
    }

    /// Retrieves length of ADARA message in buffer (not buffer capacity)
    uint32_t
    getMessageLength()
    {
        if ( !m_buf_ready )
            buildSendBuffer();

        return m_buffer_len;
    }

private:
    /// Builds the message buffer and formats ADARA translation complete message
    void
    buildSendBuffer()
    {
        uint32_t len = m_reason.length() + sizeof(ADARA::Header) + sizeof(uint32_t);
        uint16_t pad = len % 4;

        if ( pad )
            pad = 4 - pad;

        if ( len + pad > m_buffer_capacity )
        {
            delete[] m_buffer;

            m_buffer_capacity = (len + pad)*2;
            m_buffer = new char[m_buffer_capacity];
        }

        m_buffer_len = len+pad;

        uint32_t *p = (uint32_t *)m_buffer;

        *p++ = len + pad - sizeof(ADARA::Header);
        *p++ = ADARA::PacketType::TRANS_COMPLETE_V0;
        *p++ = time(0) + ADARA::EPICS_EPOCH_OFFSET;
        *p++ = 0;
        *p++ = (m_status << 16) | (m_reason.length() & 0xFFFF);
        memcpy( p, m_reason.c_str(),m_reason.length());

        // If we had to round-up the packet len, pad with zeros
        if ( pad )
            memset( m_buffer + len, 0, pad );

        m_buf_ready = true;
    }

    bool            m_buf_ready;
    char           *m_buffer;
    uint32_t        m_buffer_len;
    uint32_t        m_buffer_capacity;
    uint16_t        m_status;
    std::string     m_reason;
};

}

#endif // TRANSCOMPLETEPKT_H
