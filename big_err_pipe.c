
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>

#define __USE_GNU	/* for F_[SG]ETPIPE_SZ defines... */
#include <fcntl.h>

char *NAMED_RSYSLOG_PIPE = "/etc/rsyslog.pipes/criticalMessages";

char *SYSTEM_PIPE_MAX_SIZE = "/proc/sys/fs/pipe-max-size";

main()
{
	FILE *fp;

	int max_pipe_buffer;
	int fd;
	int cc;

	// Read System Pipe Max Size File...

	if ( (fp = fopen( SYSTEM_PIPE_MAX_SIZE, "r" )) == NULL )
	{
		fprintf( stderr,
			"\nError Opening System Pipe Max Size File:\n\t%s\n",
			SYSTEM_PIPE_MAX_SIZE );
		exit( -1 );
	}
	else
	{
		fprintf( stderr, "\nOpened System Pipe Max Size File:\n\t%s\n",
			SYSTEM_PIPE_MAX_SIZE );
	}

	if ( fscanf( fp, "%d", &max_pipe_buffer ) != 1 )
	{
		fprintf( stderr,
			"\nError Reading System Pipe Max Size from:\n\t%s\n",
			SYSTEM_PIPE_MAX_SIZE );
		exit( -2 );
	}
	else
	{
		fprintf( stderr, "\nRead System Pipe Max Size as %d from:\n\t%s\n",
			max_pipe_buffer, SYSTEM_PIPE_MAX_SIZE );
	}

	fclose( fp );

	// Set Max Buffer Size for Named RSyslog Pipe...

	if ( (fd = open( NAMED_RSYSLOG_PIPE, O_RDWR )) < 0 )
	{
		fprintf( stderr, "\nError Opening Named RSyslog Pipe:\n\t%s\n",
			NAMED_RSYSLOG_PIPE );
		exit( -3 );
	}
	else
	{
		fprintf( stderr, "\nOpened Named RSyslog Pipe:\n\t%s\n",
			NAMED_RSYSLOG_PIPE );
	}

	if ( (cc = fcntl( fd, F_GETPIPE_SZ )) < 0 )
	{
		fprintf( stderr, "\nError Getting Pipe Buffer Size for:\n\t%s\n",
			NAMED_RSYSLOG_PIPE );
		exit( -4 );
	}
	else
	{
		fprintf( stderr, "\nGot Pipe Buffer Size of %d for:\n\t%s\n",
			cc, NAMED_RSYSLOG_PIPE );
	}

	if ( (cc = fcntl( fd, F_SETPIPE_SZ, max_pipe_buffer )) < 0 )
	{
		fprintf( stderr,
			"\nError Setting Pipe Buffer Size to %d for:\n\t%s\n",
			max_pipe_buffer, NAMED_RSYSLOG_PIPE );
		exit( -5 );
	}
	else
	{
		fprintf( stderr, "\nSet Pipe Buffer Size to %d for:\n\t%s\n",
			max_pipe_buffer, NAMED_RSYSLOG_PIPE );
	}

	if ( (cc = fcntl( fd, F_GETPIPE_SZ )) < 0 )
	{
		fprintf( stderr, "\nError Verifying Pipe Buffer Size for:\n\t%s\n",
			NAMED_RSYSLOG_PIPE );
		exit( -6 );
	}
	else
	{
		if ( cc == max_pipe_buffer )
		{
			fprintf( stderr,
				"\nVerified Pipe Buffer Size of %d for:\n\t%s\n",
				cc, NAMED_RSYSLOG_PIPE );
		}
		else
		{
			fprintf( stderr,
				"\nGot Different Pipe Buffer Size of %d != %d for:\n\t%s\n",
				cc, max_pipe_buffer, NAMED_RSYSLOG_PIPE );
		}
	}

	fprintf( stderr, "\n" );

	close( fd );

	exit( 0 );
}

