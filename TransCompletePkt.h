#ifndef TRANSCOMPLETEPKT_H
#define TRANSCOMPLETEPKT_H

#include <string>

namespace STS {

class TransCompletePkt
{
public:
    TransCompletePkt()
      : m_buf_ready(false), m_buffer(0), m_buffer_len(0), m_buffer_capacity(0), m_status(0)
    {}

    TransCompletePkt( uint16_t a_status, const std::string &a_reason )
      : m_buf_ready(false), m_buffer(0), m_buffer_len(0), m_buffer_capacity(0), m_status(a_status), m_reason(a_reason)
    {}

    ~TransCompletePkt()
    {
        if ( m_buffer )
            delete[] m_buffer;
    }

    void
    setStatusReason( uint16_t a_status, const std::string &a_reason )
    {
        m_status = a_status;
        m_reason = a_reason;
        m_buf_ready = false;
    }

    const char *
    getBuffer()
    {
        if ( !m_buf_ready )
            buildSendBuffer();

        return m_buffer;
    }

    uint32_t
    getBufferLength()
    {
        if ( !m_buf_ready )
            buildSendBuffer();

        return m_buffer_len;
    }

private:
    void
    buildSendBuffer()
    {
        uint32_t len = m_reason.length() + 17;
        uint16_t pad = len % 4;

        if ( pad )
            pad = 4 - pad;

        if ( len + pad > m_buffer_capacity )
        {
            if ( m_buffer )
                delete[] m_buffer;

            m_buffer_capacity = (len + pad)*2;
            m_buffer = new char[m_buffer_capacity];
        }

        m_buffer_len = len+pad;

        uint32_t *p = (uint32_t *)m_buffer;

        *p++ = len + pad - 16;
        *p++ = 0x400500;
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
