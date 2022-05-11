//
// User ID LDAP Lookup
//
// gcc -o UserIdLdap UserIdLdap.c -lldap -llber
//

#include <stdio.h>
#include <ldap.h>

int main()
{
	LDAP *ld;

	LDAPMessage *msg;

	char *base = "ou=Users,dc=sns,dc=ornl,dc=gov";
	char *filter[3];
	char *attrs[2];

	int desired_version = LDAP_VERSION3;
	int auth_method = LDAP_AUTH_SIMPLE;

	int result;

	// Connect to the LDAP Server
	// *Don't* Pass Server URI...!
	//    -> Rely on Local System Config in /etc/openldap/ldap.conf
	//    	BASE dc=sns,dc=ornl,dc=gov
	//    	TLS_CACERTDIR /etc/openldap/cacerts
	//    	LDAP_Protocol_Version 3
	//    	URI ldaps://ldap-vip.sns.gov
	if ( ldap_initialize( &ld, NULL ) != LDAP_SUCCESS ) {
		perror("ldap_initialize failed");
		return( -1 );
	}
	printf( "Initialized to LDAP Server.\n" );

	// Set Up LDAP Search Filters...
	filter[0] = "uid=FENG_BNL";
	filter[1] = "uid=WANGDAWEI";
	filter[2] = "uid=xyz";

	// Choose Desired Attribute(s)...
	attrs[0] = "cn";
	attrs[1] = NULL;

	// Search for Each User in Turn...
	int j;
	for ( j=0; j < 3 ; j++ )
	{
		// Search LDAP Server for User ID
		if ( ldap_search_ext_s(ld, base, LDAP_SCOPE_SUBTREE, filter[j],
				(char **)attrs, 0, NULL, NULL, NULL, LDAP_NO_LIMIT, &msg)
					!= LDAP_SUCCESS)
		{
			ldap_perror( ld, "ldap_search_s" );
		}
		printf( "Searched LDAP Server for filter[%d]=%s\n", j, filter[j] );

		// Display LDAP Search Results
		int num_entries_returned = ldap_count_entries(ld, msg);
		printf( "Got %d Entries from LDAP\n", num_entries_returned );

		LDAPMessage *entry;
		for ( entry=ldap_first_entry(ld, msg); entry != NULL;
	    		entry = ldap_next_entry(ld, entry) )
		{
			printf( "LDAP Message.\n" );
			BerElement *ber;
			char *attr;
			for( attr=ldap_first_attribute(ld, entry, &ber); attr != NULL;
	    			attr = ldap_next_attribute(ld, entry, ber) ) {
				printf( "LDAP Attribute attr=%s\n", attr );

				struct berval **vals;
				if ( (vals=ldap_get_values_len(ld, entry, attr)) != NULL )
				{
					int i;
					for ( i=0; vals[i] != NULL; i++ ) {
						printf("%s:%s\n", attr, vals[i]->bv_val);
					}
				}
			}
		}
	}

	// End Session with LDAP Server
	result = ldap_unbind_s(ld);
	if ( result != 0 ) {
		fprintf( stderr, "ldap_unbind_s: %s\n", ldap_err2string(result) );
		return( -1 );
	}
	printf( "Ended Session with LDAP Server.\n" );

	return( 0 );
}

