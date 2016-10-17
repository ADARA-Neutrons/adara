//
// User ID LDAP Lookup, C API...
//

#include <string>
#include <stdint.h>

int stsLdapConnect( std::string &ldap_host, uint32_t ldap_port );

int stsLdapLookupUserName( std::string uid, std::string &user_name );

int stsLdapDisconnect(void);

