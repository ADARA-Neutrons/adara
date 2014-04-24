#include "stdafx.h"
#include "ComBusLite.h"
#include <sstream>

using namespace std;

ComBusLite::ComBusLite( const std::string &a_topic_path, const std::string &a_proc_name, unsigned long a_proc_id, const std::string &a_broker_host,
                       unsigned short a_port, const std::string &a_user, const std::string &a_pass )
: m_proc_name(a_proc_name), m_proc_id(a_proc_id), m_broker_host(a_broker_host),
  m_port(a_port), m_broker_user(a_user), m_broker_pass(a_pass), m_session( "PVStreamer" ),
  m_conn(0)
{
    try
    {
        stringstream ss;
        ss << a_topic_path << ".STATUS." << a_proc_name << "?type=topic";
        m_topic = ss.str();

        // Set reasonable timeouts (defaults are very long)
        m_session.SetOption( INTERNET_OPTION_CONNECT_TIMEOUT, 10000 );
        m_session.SetOption( INTERNET_OPTION_RECEIVE_TIMEOUT, 10000 );

        m_conn = m_session.GetHttpConnection( m_broker_host.c_str(), a_port, m_broker_user.c_str(), m_broker_pass.c_str() );
    }
    catch(...)
    {
		MessageBox( 0, "Unknown exception", "ComBusLite init failed", MB_OK );
    }
}

ComBusLite::~ComBusLite()
{
    if ( m_conn )
        delete m_conn;
}


bool
ComBusLite::sendStatus( ADARA_STATUS a_status )
{
    bool result = true;

    if ( m_conn )
    {
        CHttpFile *file = 0;

        try
        {
            DWORD ret = 0;
            stringstream data;
            string headers = "Content-Type: application/x-www-form-urlencoded\r\n";

            data << "body={\r\n\"msg_type\":\"1073741824\",\r\n";
            data << "    \"src_id\":\"" << m_proc_name << "_" << m_proc_id << "\",\r\n";
            data << "    \"dest_id\":\"0\",\r\n";
            data << "    \"timestamp\":\"" << time(0) << "\",\r\n";
            data << "    \"status\":\"" << a_status << "\"\r\n}";

            file = m_conn->OpenRequest( CHttpConnection::HTTP_VERB_POST, m_topic.c_str() );
			if ( file )
			{
				file->SendRequest( headers.c_str(), (DWORD)headers.length(), (LPVOID)data.str().c_str(), (DWORD)data.str().length() );
				file->QueryInfoStatusCode( ret );

				if ( ret == HTTP_STATUS_OK )
					result = false;

				delete file;
			}
        }
        catch( CInternetException *e )
        {
            if ( file )
            {
                file->Close();
                delete file;
            }

            // The need to call "Delete" seems to be an undocumented requrirement for using WinINet!
            // Without this call, we will leak memory for every exception thrown!
            e->Delete();
        }
        catch(...) // Just in case
        {
            if ( file )
            {
                file->Close();
                delete file;
            }
        }
    }

    return result;
}

