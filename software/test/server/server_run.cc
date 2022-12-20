#include <stdio.h>
extern "C" {
    #include "server.h" // from MicroHTTPServer
    #include "middleware.h" // from MicroHTTPServer
}

HTTPServer srv;

int main()
{
    printf("Init HTTP Server on %d.\n", MHS_PORT);
	HTTPServerInit(&srv, MHS_PORT);
	/* Run the HTTP server forever. */
	/* Run the dispatch callback if there is a new request */
    printf("Run HTTP Server Loop.\n");
	HTTPServerRunLoop(&srv, Dispatch);
	HTTPServerClose(&srv);
}

extern "C" {
	// This is to make smallprintf work on PC.
	void outbyte(int byte) { putc(byte, stdout); }
}