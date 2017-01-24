//
// User ID LDAP Lookup, C API...
//

#include <string>
#include <stdint.h>

int stsLdapConnect();

int stsLdapLookupUserName( std::string uid, std::string &user_name );

int stsLdapDisconnect(void);

