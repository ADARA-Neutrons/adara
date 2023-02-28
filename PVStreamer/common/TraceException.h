#ifndef TRACEEXCEPTION_H
#define TRACEEXCEPTION_H

#include <sstream>

#define EXCEPT(err_code,msg) throw TraceException( __FUNCTION__, __LINE__, err_code, msg )

#define EXCEPT_PARAM(err_code,msg) \
{ \
    std::stringstream s; \
    s << msg; \
    throw TraceException( __FUNCTION__, __LINE__, err_code, s.str()); \
}

#define EXCEPT_CONTEXT(e,msg) \
{ \
    std::stringstream s; \
    s << msg; \
    e.addContext( s.str()); \
}


class TraceException
{
public:
    TraceException( const char *a_file, unsigned long a_line, unsigned long a_error_code, const std::string & a_context )
        : m_file(a_file), m_line(a_line), m_error_code(a_error_code), m_context(a_context)
    {}

    virtual ~TraceException() {}

    void addContext( const std::string & a_context )
    {
        if ( a_context.size() )
        {
            m_context = a_context + "\n" + m_context;
        }
    }

    std::string toString( bool debug = false ) const
    {
        if ( debug )
        {
            std::stringstream sstr;
            sstr << m_context << std::endl;
            sstr << "(source: " << m_file << ":" << m_line << " code:" << m_error_code << ")" << std::endl;

            return sstr.str();
        }
        else
            return m_context;
    }


private:
    const char         *m_file;
    unsigned long       m_line;
    unsigned long       m_error_code;
    std::string         m_context;
};



#endif // TRACEEXCEPTION_H

// vim: expandtab

