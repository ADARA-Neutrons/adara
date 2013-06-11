#include "SubClient.h"
#include "MainWindow.h"


SubClient::SubClient( MainWindow &a_parent ) : m_parent( a_parent )
{}


SubClient::~SubClient()
{
    m_parent.detach( *this );
}


bool
SubClient::createRoute( ADARA::ComBus::MessageBase &a_msg, const std::string &a_dest_proc )
{
    return m_parent.createRoute( *this, a_msg, a_dest_proc );
}


void
SubClient::removeRoute( std::string &a_correlation_id )
{
    m_parent.removeRoute( *this, a_correlation_id );
}

