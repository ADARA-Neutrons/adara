#ifndef SUBCLIENT_H
#define SUBCLIENT_H

#include <string>
#include "ComBus.h"
#include "DASMonDefs.h"

class MainWindow;

class SubClient
{
public:
    SubClient( MainWindow &a_parent );
    ~SubClient();

    virtual void dasmonStatus( bool active ) = 0;
    virtual bool comBusControlMessage( const ADARA::ComBus::ControlMessage &a_msg ) = 0;

protected:
    bool createRoute( ADARA::ComBus::ControlMessage &a_msg, const std::string &a_dest_proc, std::string &a_correlation_id );
    void removeRoute( std::string &a_correlation_id );

private:
    MainWindow  &m_parent;

    friend class MainWindow;
};

#endif // SUBCLIENT_H
