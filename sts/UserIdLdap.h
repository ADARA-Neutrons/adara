//
// User ID LDAP Lookup, C API...
//

extern "C" {

int stsLdapConnect( const char *ldap_host );

char *stsLdapLookupUserName( char *uid );

int stsLdapDisconnect(void);

}

