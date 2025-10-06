/*! compilation: gcc -c aisgpsserver.c */

#include <glib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../csources/r3types.h"
#include "aisgps.h"

#define BUFFER_SIZE     8192
#define MAX_SIZE_OUTPUT 4096
#define MAX_SIZE_HEADER 256
#define HELP            "Synopsys : ./aisgpsserver <port> [param file name]"
#define VERSION         "V. 2025-04"
#define DESC            "AIS GPS Server by Rene Rigault"
#define PARAMETERS      "param.par"

extern Par par;                                                // defined in r3util.c
extern bool readParam (const char *fileName, bool dispInit);   // defined in r3util.c
static char parameterFileName [MAX_SIZE_FILE_NAME];

/*! send to client the response to request */
static void sendToClient (int clientFd, const char *json) {
   char header [MAX_SIZE_HEADER];
   snprintf (header, sizeof (header),
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
      "Access-Control-Allow-Headers: Content-Type\r\n"
      "Content-Length: %zu\r\n"
      "Connection: close\r\n"
      "\r\n", strlen (json));

    send (clientFd, header, strlen (header), 0);
    send (clientFd, json, strlen (json), 0);
}

/*! handle client request */
static void handleClient (int clientFd) {
   char buffer [BUFFER_SIZE];
   char json [MAX_SIZE_OUTPUT];
   ssize_t received = recv (clientFd, buffer, sizeof(buffer) - 1, 0);

   if (received < 0) {
      perror ("recv");
      close (clientFd);
      return;
   }
   buffer [received] = '\0';
   // printf ("received: %s\n", buffer);
   if (strncmp (buffer, "OPTIONS", 7) == 0) {
      printf ("Error Options\n");
      const char *optionsResponse =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
      send (clientFd, optionsResponse, strlen(optionsResponse), 0);
      close (clientFd);
      return;
   }
   // search "gps" in request
   if (strstr (buffer, "gps") != NULL) {
      printf ("gps request received\n");
      if (gpsToJson (json, sizeof (json))) {
         sendToClient (clientFd, json);
         printf ("response done\n");
      } 
      else {
         fprintf (stderr, "No gps data found\n");
         const char *err = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
         send (clientFd, err, strlen (err), 0);
      }
   }
   else if (strstr(buffer, "ais") != NULL) {
      printf ("ais request received\n");
      if (aisToJson (json, sizeof(json))) {
         sendToClient (clientFd, json); 
         printf ("response done\n");
      }
      else {
         fprintf (stderr, "No ais data found\n");
         const char *err = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
         send (clientFd, err, strlen (err), 0);
      }
   }
   else {
      const char *notFound = "HTTP/1.1 404 Not Found\r\n\r\n";
      fprintf (stderr, "Err 404\n");
      send (clientFd, notFound, strlen (notFound), 0);
   }
   close(clientFd);
}

int main (int argc, char *argv[]) {
   char buffer [MAX_SIZE_BUFFER];
   char strNmea [MAX_SIZE_LINE];
   char threadName [MAX_SIZE_NAME];
   int port, serverFd, opt, clientFd;
   struct sockaddr_in addr = {0};

   if (((argc != 2) && (argc != 3)) || ((argc == 2) && strcmp (argv [1], "-h") == 0)) {
      printf ("%s\n%s\n%s\n", HELP, VERSION, DESC);
      exit (EXIT_FAILURE);
   }

   if (argc == 3) strlcpy (parameterFileName, argv [2], sizeof (parameterFileName));
   else strlcpy (parameterFileName, PARAMETERS, sizeof (parameterFileName));

   if (! readParam (parameterFileName, false)) {
      fprintf (stderr, "Unable to read parameter file: %s\n", parameterFileName);
      exit (EXIT_FAILURE);
   }

   if (par.nNmea <= 0) {
      fprintf (stderr, "No NMEA Port available\n");
      exit (EXIT_FAILURE);
   }
   // one thread per port
   for (int i = 0; i < par.nNmea; i++) {
      snprintf (threadName, sizeof(threadName), "NMEA-%d", i);
      g_thread_new (threadName, getNmea, GINT_TO_POINTER (i));      // launch GPS or AIS
   }
   aisTableInit ();
   if (par.special) testAisTable ();
   aisToJson (buffer, sizeof (buffer));
   printf ("%s\n", buffer);
   sleep (1); // 1000 ms
   nmeaInfo (strNmea, MAX_SIZE_LINE);
   printf ("%s\n", strNmea);

   port = atoi (argv[1]);

   serverFd = socket (AF_INET, SOCK_STREAM, 0);
   if (serverFd < 0) {
      perror ("socket");
      return EXIT_FAILURE;
   }

   opt = 1;
   setsockopt (serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));

   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(port);

   if (bind (serverFd, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
      perror ("bind");
      close (serverFd);
      return EXIT_FAILURE;
   }
   if (listen (serverFd, 10) < 0) {
      perror ("listen");
      close(serverFd);
      return EXIT_FAILURE;
   }
   printf("Listening on port %d...\n", port);

   while (true) {
      struct sockaddr_in clientAddr;
      socklen_t clientLen = sizeof (clientAddr);
      clientFd = accept (serverFd, (struct sockaddr *)&clientAddr, &clientLen);
      if (clientFd < 0) {
         perror ("accept");
         continue;
      }
      handleClient (clientFd);
   }
   close (serverFd);
   // g_hash_table_destroy (aisTable);
   return EXIT_SUCCESS;
}
