//
// User ID LDAP Lookup, C API...
//

#include <string>
#include <sstream>
#include <sys/time.h>
#include <errno.h>
#include <syslog.h>
#include <ldap.h>

#include "stsdefs.h"

LDAP *stsLdapConn = NULL;

int stsLdapConnect()
{
	struct timeval network_timeout = { 3, 0 };
	struct timeval sync_timeout = { 3, 0 };

	int search_timelimit = 3;

	int cc;

	// Connect to the LDAP Server
	if ( (cc = ldap_initialize( &stsLdapConn, NULL )) != LDAP_SUCCESS )
	{
		syslog( LOG_ERR,
			"[%i] %s %s: LDAP Initialize Failed - %s",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_err2string(cc) );
		return( -1 );
	}
	syslog( LOG_INFO, "[%i] Initialized to LDAP Server", g_pid );

	// Set LDAP Network Timeout to 3 Seconds...
	if ( (cc = ldap_set_option(stsLdapConn,
			LDAP_OPT_NETWORK_TIMEOUT, &network_timeout))
				!= LDAP_OPT_SUCCESS )
	{
		syslog( LOG_ERR,
			"[%i] %s %s: LDAP Set Network Timeout Option Failed - %s",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_err2string(cc) );
	    return( -3 );
	}
	syslog( LOG_INFO, "[%i] Set LDAP Network Timeout to %ld.%ld Seconds.",
		g_pid, network_timeout.tv_sec, network_timeout.tv_usec );

	// Set LDAP Search Time Limit to 3 Seconds...
	if ( (cc = ldap_set_option(stsLdapConn,
			LDAP_OPT_TIMELIMIT, &search_timelimit))
				!= LDAP_OPT_SUCCESS )
	{
		syslog( LOG_ERR,
			"[%i] %s %s: LDAP Set Search Time Limit Option Failed - %s",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_err2string(cc) );
	    return( -4 );
	}
	syslog( LOG_INFO, "[%i] Set LDAP Search Time Limit to %d Seconds.",
		g_pid, search_timelimit );

	// Set LDAP Synchronous Timeout to 3 Seconds...
	if ( (cc = ldap_set_option(stsLdapConn,
			LDAP_OPT_TIMEOUT, &sync_timeout))
				!= LDAP_OPT_SUCCESS )
	{
		syslog( LOG_ERR,
			"[%i] %s %s: LDAP Set Synchronous Timeout Option Failed - %s",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_err2string(cc) );
	    return( -5 );
	}
	syslog( LOG_INFO,
		"[%i] Set LDAP Synchronous Timeout to %ld.%ld Seconds.",
		g_pid, sync_timeout.tv_sec, sync_timeout.tv_usec );

	return( 0 );
}

int stsLdapLookupUserName( std::string uid, std::string &user_name )
{
	LDAPMessage *msg;

	std::string base = "ou=Users,dc=sns,dc=ornl,dc=gov";

	std::string filter;

	const char *attrs[2];

	struct timeval search_timeout = { 3, 0 };

	int cc;

	// Verify Valid UID String...
	if ( uid.empty() )
	{
		syslog( LOG_ERR, "[%i] %s %s: Invalid User ID String! [%s]",
			g_pid, "STS Error:", "stsLdapLookupUserName()", uid.c_str() );
		return( -1 );
	}

	// Set Up LDAP Search Filters...
	filter = "uid=" + uid;

	// Choose Desired Attribute(s)...
	attrs[0] = "cn";
	attrs[1] = NULL;

	// Search LDAP Server for User ID
	if ( (cc = ldap_search_ext_s(stsLdapConn,
			base.c_str(), LDAP_SCOPE_SUBTREE,
			filter.c_str(), (char **)attrs, 0, NULL, NULL,
			&search_timeout, LDAP_NO_LIMIT, &msg)) != LDAP_SUCCESS )
	{
		syslog( LOG_ERR, "[%i] %s %s: LDAP Search Failed - %s",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_err2string(cc) );
		return( -2 );
	}
	syslog( LOG_INFO, "[%i] %s: Searched LDAP Server for filter=[%s]",
		g_pid, "stsLdapLookupUserName()", filter.c_str() );

	// Display LDAP Search Results
	int num_entries_returned = ldap_count_entries(stsLdapConn, msg);
	syslog( LOG_INFO, "[%i] %s: Got %d Entries from LDAP",
		g_pid, "stsLdapLookupUserName()", num_entries_returned );

	LDAPMessage *entry;
	for ( entry=ldap_first_entry(stsLdapConn, msg); entry != NULL;
    		entry = ldap_next_entry(stsLdapConn, entry) )
	{
		BerElement *ber;
		char *attr;
		for ( attr=ldap_first_attribute(stsLdapConn, entry, &ber);
				attr != NULL;
    			attr = ldap_next_attribute(stsLdapConn, entry, ber) )
		{
			// Found LDAP User Name! :-D
			if ( !strcmp( attr, attrs[0] ) )
			{
				struct berval **vals;
				if ( (vals=ldap_get_values_len(stsLdapConn, entry, attr))
						!= NULL )
				{
					if ( vals[0] != NULL )
					{
						user_name = std::string( vals[0]->bv_val );
						syslog( LOG_INFO,
							"[%i] %s: Found LDAP User Name [%s]",
							g_pid, "stsLdapLookupUserName()",
							user_name.c_str() );
						ldap_value_free_len(vals);
						ber_free(ber, 0);
						ldap_msgfree(msg);
						return( 0 );
					}
				}
				ldap_value_free_len(vals);
			}
		}
		ber_free(ber, 0);
	}

	syslog( LOG_ERR, "[%i] %s %s: LDAP User Name Not Found for uid=[%s]!",
		g_pid, "STS Error:", "stsLdapLookupUserName()", uid.c_str() );
	ldap_msgfree(msg);
	return( -3 );
}

int stsLdapDisconnect(void)
{
	int cc;

	// End Session with LDAP Server
	if ( (cc = ldap_unbind_ext_s(stsLdapConn, NULL, NULL)) != 0 )
	{
		syslog( LOG_ERR, "[%i] %s %s: ldap_unbind_s: %s",
			g_pid, "STS Error:", "stsLdapDisconnect()",
			ldap_err2string(cc) );
		return( -1 );
	}
	syslog( LOG_INFO, "[%i] Ended Session with LDAP Server.", g_pid );

	return( 0 );
}

