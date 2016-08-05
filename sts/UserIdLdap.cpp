//
// User ID LDAP Lookup, C API...
//

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <ldap.h>

#include "stsdefs.h"

extern "C" {

LDAP *stsLdapConn = NULL;

int stsLdapConnect( const char *ldap_host )
{
	char ldap_uri[255];

	int desired_version = LDAP_VERSION3;

	int cc;

	// Use Default LDAP Host, If Left Unspecified...
	if ( ldap_host == NULL )
		ldap_host = (const char *) "data.sns.gov";

	// Check for LDAP Host Name Overflow...
	else if ( strlen( ldap_host ) > sizeof( ldap_uri ) - 7 - 1 - 6 - 1 )
	{
		syslog( LOG_ERR, "[%i] %s %s: LDAP Server Host Name Too Long! [%s]",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_host );
		return( -1 );
	}

	// Build LDAP URI from Server Host Name and Default Port...
	sprintf( ldap_uri, "ldap://%s:%d", ldap_host, LDAP_PORT );

	// Connect to the LDAP Server
	if ( (cc = ldap_initialize( &stsLdapConn, ldap_uri )) != LDAP_SUCCESS )
	{
		syslog( LOG_ERR, "[%i] %s %s: LDAP Initialize Failed - %s",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_err2string(cc) );
		return( -2 );
	}
	syslog( LOG_INFO, "[%i] Connected to LDAP Server %s at port %d",
		g_pid, ldap_host, LDAP_PORT );

	// Set LDAP Protocol to Version 3
	if ( (cc = ldap_set_option(stsLdapConn,
			LDAP_OPT_PROTOCOL_VERSION, &desired_version))
				!= LDAP_OPT_SUCCESS )
	{
		syslog( LOG_ERR, "[%i] %s %s: LDAP Set Option Failed - %s",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_err2string(cc) );
	    return( -3 );
	}
	syslog( LOG_INFO, "[%i] Set LDAP Protocol to Version 3.", g_pid );

	return( 0 );
}

char *stsLdapLookupUserName( char *uid )
{
	LDAPMessage *msg;

	const char *base = "ou=Users,dc=sns,dc=ornl,dc=gov";

	char filter[1024];

	const char *attrs[2];

	int cc;

	// Verify Valid UID String...
	if ( uid == NULL && *uid == '\0' )
	{
		syslog( LOG_ERR, "[%i] %s %s: Invalid User ID String! [%s]",
			g_pid, "STS Error:", "stsLdapLookupUserName()",
			( uid ) ? uid : "(null)" );
		return( (char *) NULL );
	}

	// Check for UID String Overflow...
	if ( strlen( uid ) > sizeof( filter ) - 4 - 1 )
	{
		syslog( LOG_ERR, "[%i] %s %s: User ID String Too Long! [%s]",
			g_pid, "STS Error:", "stsLdapLookupUserName()", uid );
		return( (char *) NULL );
	}

	// Set Up LDAP Search Filters...
	sprintf( filter, "uid=%s", uid );

	// Choose Desired Attribute(s)...
	attrs[0] = "cn";
	attrs[1] = NULL;

	// Search LDAP Server for User ID
	if ( (cc = ldap_search_ext_s(stsLdapConn, base, LDAP_SCOPE_SUBTREE,
			filter, (char **)attrs, 0, NULL, NULL, NULL, LDAP_NO_LIMIT,
			&msg)) != LDAP_SUCCESS )
	{
		syslog( LOG_ERR, "[%i] %s %s: LDAP Search Failed - %s",
			g_pid, "STS Error:", "stsLdapConnect()", ldap_err2string(cc) );
		return( (char *) NULL );
	}
	syslog( LOG_INFO, "[%i] %s: Searched LDAP Server for filter=[%s]",
		g_pid, "stsLdapLookupUserName()", filter );

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
			struct berval **vals;
			if ( (vals=ldap_get_values_len(stsLdapConn, entry, attr))
					!= NULL )
			{
				int i;
				for ( i=0; vals[i] != NULL; i++ )
				{
					// Found LDAP User Name! :-D
					if ( !strcmp( attr, attrs[0] ) )
					{
						syslog( LOG_INFO,
							"[%i] %s: Found LDAP User Name [%s]",
							g_pid, "stsLdapLookupUserName()",
							vals[i]->bv_val );
						return( strdup( vals[i]->bv_val) );
					}
				}
			}
		}
	}

	syslog( LOG_ERR, "[%i] %s %s: LDAP User Name Not Found for uid=[%s]!",
		g_pid, "STS Error:", "stsLdapLookupUserName()", uid );
	return( (char *) NULL );
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

}

