//
// User ID LDAP Lookup, C API...
//

#include <string>
#include <stdint.h>

int stcLdapConnect();

int stcLdapLookupUserName( std::string uid, std::string &user_name );

int stcLdapDisconnect(void);

