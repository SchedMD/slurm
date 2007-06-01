/*
 * This file contains the routines that allow use of the Secure Socket Layer
 * (SSL).  If the SSL routines are not available, then these routines
 * return -1.
 */

#ifdef HAVE_SSL
#include "ssl.h"
#include "ssllib.h"
#define DEFAULT_SERVER_CERT_FILE "ssl/server_cert.txt"
#define DEFAULT_SERVER_KEY_FILE  "ssl/server_key.der"
#define DEFAULT_SERVER_KEY_PASSWORD "jaeger\n"

SSLHandle *ssl_handle;
int ssl_mode = 0;

char cert_filename[1024];
char key_filename[1024];
char key_passwd[100];

int Init_ssl()
{
    strcpy(cert_filename, DEFAULT_SERVER_CERT_FILE);
    strcpy(key_filename, DEFAULT_SERVER_KEY_FILE);
    strcpy(key_passwd, DEFAULT_SERVER_KEY_PASSWORD);
    return 0;
}

int Set_ssl_paths( c_file, k_file, k_pass )
char *c_file, *k_file, *k_pass;
{
    strncpy(cert_filename, 1024, c_file );
    strncpy(key_filename, 1024, k_file );
    strncpy(key_passwd, 100, k_pass );
    return 0;
}

FILE *cert_file;
FILE *key_file;
RSAPrivateKey *key;
unsigned char *cert;
unsigned int certlen;
int rc;

/* 
 * This is the first half of the SSL setup - it handles the server's
 * side of the connection.
 */
int Setup_ssl( )
{
    cert_file = fopen(cert_filename, "r");
    if (!cert_file)
    {
	fprintf(stderr, "Could not open server certificate file %s.\n",
		cert_filename );
	exit(1);
    }

    rc = S_ReadCertificate(&cert, &certlen, cert_file);
    if (rc != 0)
    {
	fprintf(stderr, "Bad certificate in %s.\n", cert_filename);
	exit(1);
    }
    fclose(cert_file);

    key_file = fopen(key_filename, "r");
    if (!key_file)
    {
	fprintf(stderr, "Could not open server key file.\n");
	exit(1);
    }

    key = PKCS8_ReadPrivateKey(key_file, key_passwd);
    if (!key)
    {
	fprintf(stderr, "Bad key in %s.\n", key_filename);
	exit(1);
    }
    fclose(key_file);

    SSL_ServerInfo(cert, certlen, key);
    return 0;
}

/*
 * This is the part that creates the SSL socket and sets up the read/write.
 * Note that this code reads the client user from the clear-text socket
 * using getline.
 */
int Create_ssl_handle( )
{
    int rc;
    
    /* 
     * Drop into SSL mode
     */
    ssl_mode = 1;
    notice("SSL mode");
    ssl_handle = SSL_Create(fd, SSL_ENCRYPT | SSL_NO_PROXY);
    rc = SSL_Handshake(ssl_handle, SSL_HANDSHAKE_AS_SERVER);
    if (rc < 0)
    {
	failure("SSL_Handshake() failed for server.\n");
    }
    
    if (!getline(client_user, sizeof(client_user)))
    {
	failure("No client user after %%ssl directive");
    }
    return 0;
}

#else
int Init_ssl()
{
    return -1;
}
int Set_ssl_paths( c_file, k_file, k_pass )
char *c_file, *k_file, *k_pass;
{
    return -1;
}
int Setup_ssl( )
{
    return -1;
}
int Create_ssl_handle( )
{
    return -1;
}

#endif
