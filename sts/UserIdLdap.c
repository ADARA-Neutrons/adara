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

	char *ldap_host = "ldap-vip.sns.gov";
	char ldap_uri[255];

	char *base = "ou=Users,dc=sns,dc=ornl,dc=gov";
	char *filter[3];
	char *attrs[2];

	int desired_version = LDAP_VERSION3;
	int auth_method = LDAP_AUTH_SIMPLE;

	int result;

	// Connect to the LDAP Server
	sprintf( ldap_uri, "ldap://%s:%d", ldap_host, LDAP_PORT );
	if ( ldap_initialize( &ld, ldap_uri ) != LDAP_SUCCESS ) {
		perror("ldap_initialize failed");
		return( -1 );
	}
	printf( "Initialized to LDAP Server %s at port %d\n",
		ldap_host, LDAP_PORT );

	// Set LDAP Protocol to Version 3
	if ( ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &desired_version)
			!= LDAP_OPT_SUCCESS ) {
	    ldap_perror(ld, "ldap_set_option failed!");
	    return( -1 );
	}
	printf( "Set LDAP Protocol to Version 3.\n" );

	// Set Up LDAP Search Filters...
	filter[0] = "uid=FENG_BNL";
	filter[1] = "uid=WANGDAWEI";
	filter[2] = "uid=y8y";

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

