/*! RCube is a routing software for sailing. 
 * It computes the optimal route from a starting point (pOr) to a destination point (pDest) 
 * at a given START_TIME, using GRIB weather data and a boat polar diagram.
 * to launch : r3server <port number> [parameter file]
 */
  
#include <stdbool.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>
#include <locale.h>
#include "glibwrapper.h"
#include "r3types.h"
#include "r3util.h"
#include "engine.h"
#include "grib.h"
#include "readgriball.h"
#include "polar.h"
#include "inline.h"
#include "option.h"
#include "common.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h> // for signal(SIGPIPE, SIG_IGN)

#define SYNOPSYS               "<port> | -<option> [<parameter file>]"
#define MAX_SIZE_REQUEST       2048        // Max size from request client
#define MAX_SIZE_RESOURCE_NAME 256         // Max size polar or grib name
#define PATTERN                "GFS"
#define MAX_SIZE_FEED_BACK     1024
#define ADMIN_LEVEL            10          // level of authorization for administrator
#define BIG_BUFFER_SIZE        (300*MILLION)
#define MAX_SIZE_MESS          100000
#define GPX_ROUTE_FILE_NAME    "gpxroute.tmp"

char *bigBuffer = NULL;

// level of authorization
//const int typeLevel [16] = {0, 1, 0, 0, 0, 0, 0, 0, 0, ADMIN_LEVEL, 0, 0, 0, 0, 0, 0};        
const int typeLevel [17] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // everybody access to all       
 
char parameterFileName [MAX_SIZE_FILE_NAME];

/*!
 * @brief Retrieves the real client IP address from HTTP headers.
 * 
 * @param headers A string containing the HTTP headers.
 * @param clientAddress Buffer to store the client's IP address.
 * @param bufferSize Size of the clientAddress buffer.
 * @return true if the IP address was successfully found and stored, false if not found or error.
 */
static bool getRealIPAddress (const char* headers, char* clientAddress, size_t bufferSize) {
   const char* headerName = "X-Real-IP: ";
   const char* headerStart = strstr (headers, headerName);

   if (headerStart) {
      headerStart += strlen (headerName);
      while (*headerStart == ' ') headerStart++;  // Skip any whitepaces
        
      const char* headerEnd = strstr(headerStart, "\r\n");
      if (headerEnd) {
         size_t ipLength = headerEnd - headerStart;
         if (ipLength < bufferSize) {
            g_strlcpy (clientAddress, headerStart, ipLength + 1);
            g_strstrip (clientAddress); 
            return true;
         } else {
            fprintf (stderr, "In getRealIPAddress: IP address length exceeds buffer size.");
            return false;
         }
      }
   }
   clientAddress [0] = '\0';
   return false;
}

/*! extract user agent */
static char* extractUserAgent (const char* saveBuffer) {
   const char* headerName = "User-Agent: ";
   const char* userAgentStart = strstr (saveBuffer, headerName);
   if (userAgentStart) {
      userAgentStart += strlen (headerName);
      const char* userAgentEnd = strstr  (userAgentStart, "\r\n");
      if (userAgentEnd) {
         return strndup (userAgentStart, userAgentEnd - userAgentStart);
      }
   }
   return NULL;  // No user agent found
}

/*! extract level */
static int extractLevel (const char* buffer) {
   if (!par.authent) return ADMIN_LEVEL; // No autentication, get highest level
   const char* headerName = "X-User-Level:";
   const char* levelStart = strstr (buffer, headerName);
   if (levelStart) return atoi (levelStart + strlen (headerName));
   return 0;  // No X_User-Level found
}

/*! compare level of authorization with request */
static bool allowedLevel (ClientRequest *clientReq) {
   if ((clientReq->type == REQ_KILL) && (clientReq->level == ADMIN_LEVEL)) {
      return true;
   }
   if ((clientReq->type == 1) && (strstr (clientReq->model, "GFS") != NULL)) {
      printf ("GFS allowed whatever the level for type 1 request\n");    
      return true; // GFS allowed to anyone
   }
   if ((clientReq->level >= 0) && (clientReq->level <= ADMIN_LEVEL)) {
      printf ("Allowed associated to typeLevel: %d\n", typeLevel [clientReq->type]);    
      return clientReq->level >= typeLevel [clientReq->type]; 
   }
   return false; // Level out of range
} 

/*! log client Request 
    side effect: dataReq is modified */
static void logRequest (const char* fileName, const char *date, int serverPort, const char *remote_addr, \
   char *dataReq, const char *userAgent, ClientRequest *client, double duration) {

   char newUserAgent [MAX_SIZE_LINE];
   g_strlcpy (newUserAgent, userAgent, sizeof newUserAgent);
   g_strdelimit (newUserAgent, ";", ':'); // to avoid ";" the CSV delimiter inside field
   char *startAgent = strchr (newUserAgent, '('); // we delete what is before ( if exist
   if (startAgent == NULL) startAgent = newUserAgent;

   FILE *logFile = fopen (fileName, "a");
   if (logFile == NULL) {
      fprintf (stderr, "In logRequest, Error opening log file: %s\n", fileName);
      return;
   }
   g_strstrip (dataReq);
   g_strdelimit (dataReq, "\r\n", ' ');
   
   fprintf (logFile, "%s; %d; %-16.16s; %-40.40s; %2d; %6.2lf; %.50s\n", 
      date, serverPort, remote_addr, startAgent, client->type, duration, dataReq);
   fclose (logFile);
}

/*! Translate extension in MIME type */
static const char *getMimeType (const char *path) {
   const char *ext = strrchr(path, '.');
   if (!ext) return "application/octet-stream";
   if (strcmp(ext, ".html") == 0)   return "text/html";
   if (strcmp(ext, ".css") == 0)    return "text/css";
   if (strcmp(ext, ".js") == 0)     return "application/javascript";
   if (strcmp(ext, ".png") == 0)    return "image/png";
   if (strcmp(ext, ".jpg") == 0)    return "image/jpeg";
   if (strcmp(ext, ".gif") == 0)    return "image/gif";
   if (strcmp(ext, ".txt") == 0)    return "text/plain";
   if (strcmp(ext, ".par") == 0)    return "text/plain";
   if (strcmp(ext, "geojson") == 0) return "application/geo+json; charset=utf-8";
   return "application/octet-stream";
}

/*! serve static file */
static void serveStaticFile (int client_socket, const char *requested_path) {
   char filepath [2048];
   snprintf (filepath, sizeof filepath, "%s%s", par.web, requested_path);

   char *q = strchr(filepath, '?'); // cut after ? to avoid issue with path/r3.js?v=0.2
   if (q) *q = '\0';
   printf ("File Path: %s\n", filepath);

   // Check if file exist
   struct stat st;
   if (stat (filepath, &st) == -1 || S_ISDIR(st.st_mode)) {
      const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
      send (client_socket, not_found, strlen(not_found), 0);
      fprintf (stderr, "In serveStaticFile, Error 404\n");
      return;
   }

   // File open
   const int file = open (filepath, O_RDONLY);
   if (file == -1) {
      const char *error = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\n500 Internal Server Error";
      send (client_socket, error, strlen(error), 0);
      fprintf (stderr, "In serveStaticFile, Error 500\n");
      return;
   }

   // send HTTP header and MIME type
   char header [256];
   snprintf (header, sizeof header, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n",
          getMimeType(filepath), st.st_size);
   send (client_socket, header, strlen(header), 0);

   // Read end send file
   char buffer [1024];
   ssize_t bytesRead;
   while ((bytesRead = read(file, buffer, sizeof buffer)) > 0) {
      ssize_t totalSent = 0;
      while (totalSent < bytesRead) {
         ssize_t sent = send(client_socket, buffer + totalSent, bytesRead - totalSent, 0);
         if (sent <= 0) {
            perror("send"); // we stop in a clean way if client disconnects
            close(file);
            fprintf(stderr, "In serveStaticFile, Client disconnected while sending file\n");
            return;
         }
         totalSent += sent;
      }
   }
   close (file);
   printf ("✅ serveStaticFile OK\n");
}

/*!
 * @brief Return the current memory usage (resident set size) in kilobytes.
 * @return int Memory usage in KB, or -1 on error.
 */
int memoryUsage (void) {
   FILE *fp = fopen("/proc/self/status", "r");
   if (!fp) return -1;
   char line[256];
   int mem = -1;

   while (fgets (line, sizeof line, fp)) {
      if (strncmp (line, "VmRSS:", 6) == 0) {
         // Example line: "VmRSS:      12345 kB"
         sscanf (line + 6, "%d", &mem);  // skip "VmRSS:"
         break;
      }
   }
   fclose(fp);
   return mem;  // in KB
}

/*! Provide system information */
static char *testToJson (int serverPort, const char *clientIP, const char *userAgent, int level, char *out, size_t maxLen) {
   char strWind [MAX_SIZE_LINE] = "";
   char strCurrent [MAX_SIZE_LINE] = "";
   char strMem [MAX_SIZE_LINE] = "";
   char str [MAX_SIZE_TEXT_FILE] = "";

   formatThousandSep (strWind, sizeof strWind, sizeof(FlowP) * (zone.nTimeStamp + 1) * zone.nbLat * zone.nbLon);
   formatThousandSep (strCurrent, sizeof strCurrent, sizeof(FlowP) * (currentZone.nTimeStamp + 1) * currentZone.nbLat * currentZone.nbLon);
   formatThousandSep (strMem, sizeof strMem, memoryUsage ()); // KB ! 
   
   snprintf (out, maxLen,
      "{\n  \"Prog-version\": \"%s, %s, %s\",\n"
      "  \"API server port\": %d,\n"
      "  \"Grib Reader\": \"%s\",\n"
      "  \"Memory for Grib Wind\": \"%s\",\n"
      "  \"Memory for Grib Current\": \"%s\",\n"
      "  \"Compilation-date\": \"%s\",\n"
      "  \"PID\": %d,\n"
      "  \"Memory usage in KB\": \"%s\",\n"
      "  \"Client IP Address\": \"%s\",\n"
      "  \"User Agent\": \"%s\",\n"
      "  \"Authorization-Level\": %d\n}\n",
      PROG_NAME, PROG_VERSION, PROG_AUTHOR, serverPort, gribReaderVersion (str, sizeof str), 
      strWind, strCurrent, __DATE__, getpid (), strMem, clientIP, userAgent, level
   );
   return out;
}

/*! send over network binary data */
void sendBinaryResponse(int sock, const void *data, size_t len, const char *shortnames) {
   char header[512];
   const char *corsHeaders =
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
      "Access-Control-Allow-Headers: Content-Type\r\n"
      "Access-Control-Expose-Headers: X-Shortnames\r\n"; // Important to read X-Shortnames

   int n = snprintf(header, sizeof header,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/octet-stream\r\n"
              "X-Shortnames: %s\r\n"
              "%s"
              "Content-Length: %zu\r\n"
              "Connection: close\r\n"
              "\r\n",
              shortnames, corsHeaders, len);

   if ((n < 0)  || (size_t)n >= sizeof header) {
      fprintf (stderr, "In sendBinaryResponse, Error n:%d\n", n);
      return;
   }
   send(sock, header, (size_t)n, 0);
   send(sock, data, len, 0);
}

/*! launch action and returns outBuffer after execution */
static bool launchAction (int serverPort, int sock, ClientRequest *clientReq, 
             const char *date, const char *clientIPAddress, const char *userAgent, char *outBuffer, size_t maxLen) {
   char tempFileName [MAX_SIZE_FILE_NAME];
   char checkMessage [MAX_SIZE_TEXT];
   char errMessage [MAX_SIZE_TEXT] = "";
   char directory [MAX_SIZE_DIR_NAME];
   char str [MAX_SIZE_NAME];
   char *strTemp = NULL;
   int  len;
   bool resp = true;
   bigBuffer [0] = '\0';
   // printf ("client.req = %d\n", clientReq->type);
   switch (clientReq->type) {
   case REQ_KILL:
      printf ("Killed on port: %d, At: %s, By: %s\n", serverPort, date, clientIPAddress);
      snprintf (outBuffer, maxLen, "{\n  \"killed_on_port\": %d,\n  \"date\": \"%s\",\n  \"by\": \"%s\"\n}\n", serverPort, date, clientIPAddress);
      break;
   case REQ_TEST:
      testToJson (serverPort, clientIPAddress, userAgent, clientReq->level, outBuffer, maxLen);
      break;
   case REQ_ROUTING:
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof checkMessage)) {
         competitors.runIndex = 0;
         routingLaunch ();
         if (route.ret == ROUTING_ERROR) {
            snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "Routing failed");
         }
         else routeToJson (&route, clientReq->isoc, clientReq->isoDesc, outBuffer, maxLen);
      }
      else {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", checkMessage);
      }
      break;
   case REQ_COORD:
      if (clientReq->nBoats > 0) {
         infoCoordToJson (clientReq->boats [0].lat, clientReq->boats [0].lon, clientReq, outBuffer, maxLen);
      }
      else {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"No Boat\"\n}\n");
      }
      break;
   case REQ_FORBID:
      forbidToJson (outBuffer, maxLen);
      break;
   case REQ_POLAR:
      if (clientReq->polarName [0] != '\0') {
         if (strstr (clientReq->polarName, "wavepol")) {
            polToStrJson (false, clientReq->polarName, "wavePolarName", outBuffer, maxLen);
         }
         else {
            polToStrJson (true, clientReq->polarName , "polarName", outBuffer, maxLen);
         }
         break;
      }
      polToStrJson (true, par.polarFileName, "polarName", outBuffer, maxLen);
      break;
   case REQ_GRIB:
      if (clientReq->model [0] != '\0' && clientReq->gribName [0] == '\0') { // there is a model specified but no grib file
         printf ("model: %s\n", clientReq->model);
         snprintf (directory, sizeof directory, "%s%sgrib", par.workingDir, hasSlash (par.workingDir) ? "" : "/"); 
         if (!mostRecentFile (directory, ".gr", clientReq->model, clientReq->gribName, sizeof clientReq->gribName)) {
            snprintf (outBuffer, maxLen, "{\"_Error\": \"No grib with model: %s\"}\n", clientReq->model);
            break;
         }
      }
      if (clientReq->gribName [0] != '\0') gribToStrJson (clientReq->gribName, outBuffer, maxLen);
      else snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No Grib");
      break;
   case REQ_DIR:
      listDirToStrJson (par.workingDir, clientReq->dirName, clientReq->sortByName, NULL, filter, outBuffer, maxLen);
      break;
   case REQ_PAR_RAW:
      // yaml style if model = 1
      bool yaml = clientReq->model [0] == 'y';
      buildRootName (TEMP_FILE_NAME, tempFileName, sizeof tempFileName);
      writeParam (tempFileName, false, false, yaml);
      strTemp = readTextFile (tempFileName, errMessage, sizeof errMessage);
      if (strTemp == NULL) {
         snprintf (outBuffer, maxLen, "{\n  \"_Error\": \"%s\"\n}\n", errMessage);
         break;
      }
      strlcat (outBuffer, strTemp, maxLen);
      free (strTemp);
      if (yaml) normalizeSpaces (outBuffer);
      break;
   case REQ_PAR_JSON:
      paramToStrJson (&par, outBuffer, maxLen);
      break;
   case REQ_INIT:
      if (! initContext (parameterFileName, PATTERN))
         snprintf (outBuffer, maxLen, "{\n  \"_Error\": \"Init failed\",\n  \"serverPort\": %d\n}\n", serverPort);
      else
         snprintf (outBuffer, maxLen, "{\n  \"message\": \"Init done\",\n  \"serverPort\": %d\n}\n", serverPort);
      break;
   case REQ_FEEDBACK:
         handleFeedbackRequest (par.feedbackFileName, date, clientIPAddress, clientReq->feedback);
         snprintf (outBuffer, maxLen, "{\"_Feedback\": \"%s\"}\n", "OK");
      break;
   case REQ_DUMP_FILE:  // raw dump
      buildRootName (clientReq->fileName, tempFileName, sizeof tempFileName);
      strTemp = readTextFile (tempFileName, errMessage, sizeof errMessage);
      if (strTemp == NULL) {
         snprintf (outBuffer, maxLen, "{\n  \"_Error\": \"%s\"\n}\n", errMessage);
         break;
      }
      strlcat (outBuffer, strTemp, maxLen);
      free (strTemp);
      break;
   case REQ_MARKS: 
      if (! readMarkCSVToJson (par.marksFileName, outBuffer, maxLen)) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"Reading Mark File %s\"}\n", par.marksFileName);
      }
      break;
   case REQ_NEAREST_PORT:
      if (clientReq->nWp > 0)
         nearestPortToStrJson (clientReq->wp[0].lat, clientReq->wp[0].lon, outBuffer, maxLen);
      else snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No coordinates found");
      break;
   case REQ_CHECK_GRIB: 
      len = strlen (clientReq->gribName);
      bool hasGrib = len > 0 && clientReq->gribName [len - 1] != '/';
      len = strlen (clientReq->currentGribName);
      bool hasCurrentGrib = len > 0 && clientReq->currentGribName [len - 1] != '/';
      if (!hasGrib  && !hasCurrentGrib) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "Either wind grib or current grib required");
         break;
      }
      if (hasGrib && !(updateWindGrib (clientReq, outBuffer, maxLen))) break;
      if (hasCurrentGrib && !(updateCurrentGrib (clientReq, outBuffer, maxLen))) break;

      checkGribToStr (hasCurrentGrib, outBuffer, maxLen);
      if (outBuffer [0] == '\0') g_strlcpy (outBuffer, "All is OK\n", maxLen);
      break;
   case REQ_GPX_ROUTE:
      buildRootName(GPX_ROUTE_FILE_NAME, tempFileName, sizeof tempFileName);
      if (route.n == 0 || !exportRouteToGpx (&route, tempFileName)) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No route");
         break;
      }
      strTemp = readTextFile (tempFileName, errMessage, sizeof errMessage);
      if (strTemp == NULL) {
         snprintf (outBuffer, maxLen, "{\n  \"_Error\": \"%s\"\n}\n", errMessage);
         break;
      }
      strlcat (outBuffer, strTemp, maxLen);
      free (strTemp);
      break;
   case REQ_GRIB_DUMP:
      if (clientReq->model [0] != '\0' && clientReq->gribName [0] == '\0') { // there is a model specified but no grib file
         printf ("model: %s\n", clientReq->model);
         snprintf (directory, sizeof directory, "%s%sgrib", par.workingDir, hasSlash (par.workingDir) ? "" : "/"); 
         mostRecentFile (directory, ".gr", clientReq->model, clientReq->gribName, sizeof clientReq->gribName);
      }
      if (clientReq->gribName [0] == '\0') {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No Grib");
         break;
      }
      updateWindGrib (clientReq, checkMessage, sizeof checkMessage);
      size_t dataLen = 0;

      if (clientReq->onlyUV) strlcpy (str, "uv", 3);
      else buildInitialOfShortNameList(&zone, str, sizeof str);

      float *buf = buildUVGWarray(&zone, str, tGribData[WIND], &dataLen); // dataLen in number of float
      sendBinaryResponse(sock, buf, dataLen * sizeof(float), str);
      printf("✅ Send Binary float array with: Len=%zu, Bytes=%zu, Shortnames=%s\n\n",
         dataLen, dataLen * sizeof(float), str);

      free(buf);
      resp = false;
      break;
   default:;
   }
   return resp;
}

/*! robust send */
static int sendAll (int fd, const void *buf, size_t len) {
   const unsigned char *p = buf;
   while (len > 0) {
      const ssize_t n = send(fd, p, len, 0);
      if (n < 0) {
         if (errno == EINTR) continue;
         if (errno == EAGAIN || errno == EWOULDBLOCK) continue; // or poll/select
         return -1;
      }
      p += (size_t)n;
      len -= (size_t)n;
   }
   return 0;
}

/*! Handle client connection and launch actions */
static bool handleClient (int serverPort, int clientFd, struct sockaddr_in *client_addr) {
   char saveBuffer [MAX_SIZE_REQUEST];
   char buffer [MAX_SIZE_REQUEST] = "";
   char clientIPAddress [MAX_SIZE_LINE];
   bool resp = true;

   // read HTTP request
   const int bytes_read = recv (clientFd, buffer, sizeof buffer - 1, 0);
   if (bytes_read <= 0) {
     return false;
   }
   buffer [bytes_read] = '\0'; // terminate string
   // printf ("Client Request: %s\n", buffer);
   g_strlcpy (saveBuffer, buffer, sizeof buffer);

   if (! getRealIPAddress (buffer, clientIPAddress, sizeof clientIPAddress)) { // try if proxy 
      // Get client IP address if IP address not found with proxy
      char remoteAddr [INET_ADDRSTRLEN];
      inet_ntop (AF_INET, &(client_addr->sin_addr), remoteAddr, INET_ADDRSTRLEN); // not used
      g_strlcpy (clientIPAddress, remoteAddr, INET_ADDRSTRLEN);
   }

   // Extract HTTP first line request
   char *requestLine = strtok (buffer, "\r\n");
   if (!requestLine) return false;
   
   // check if Rest API (POST) or static file (GET)
   if (strncmp (requestLine, "POST", 4) != 0) {
      printf ("📥 GET Request, static file: %s\n", requestLine);
      // static file
      const char *requested_path = strchr (requestLine, ' '); // space after "GET"
      if (!requested_path) {
        return false;
      }
      requested_path++; // Pass space

      char *end_path = strchr (requested_path, ' ');
        if (end_path) {
      *end_path = '\0'; // Terminate string
      }

      if (strcmp(requested_path, "/") == 0) {
         requested_path = "/index.html"; // Default page
      }

      serveStaticFile (clientFd, requested_path);
      return true; // stop
   }

   // Extract request body
   char *postData = strstr (saveBuffer, "\r\n\r\n");
   if (postData == NULL) {
      return false;
   }

   char* userAgent = extractUserAgent (saveBuffer);
   postData += 4; // Ignore HTTP request separators
   printf ("🟠 POST Request:\n%s\n", postData);

   // data for log
   const double start = monotonic (); 
   const char *date = getCurrentDate ();

   if (! decodeFormReq (postData, &clientReq)) {
      const char *errorResponse = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nError 400\n";
      fprintf (stderr, "In handleClient, Error: %s", errorResponse);
      send (clientFd, errorResponse, strlen(errorResponse), 0);
      return false;
   }
   clientReq.level = extractLevel (saveBuffer);
   printf ("user level: %d\n", clientReq.level);

   const char *corsHeaders = "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n";

   if (! allowedLevel (&clientReq)) {
      g_strlcpy (bigBuffer, "{\"_Error\": \"Too low level of authorization\"}\n", BIG_BUFFER_SIZE);
   }
   else {
      resp = launchAction (serverPort, clientFd, &clientReq, date, clientIPAddress, userAgent, bigBuffer, BIG_BUFFER_SIZE);
   }
   if (resp) {
      char header [512];
      const size_t bigBufferLen = strlen (bigBuffer);
      const int headerLen = snprintf (header, sizeof header,
         "HTTP/1.1 200 OK\r\n"
         "Content-Type: application/json\r\n"
         "%s"
         "Content-Length: %zu\r\n"
         "\r\n",
         corsHeaders, bigBufferLen);

      if (sendAll (clientFd, header, (size_t) headerLen) < 0) return false;
      if (sendAll (clientFd, bigBuffer, bigBufferLen) < 0) return false;
      printf ("✅ Response sent to client. Size: %zu\n\n", headerLen + bigBufferLen);
   }

   const double duration = monotonic () - start; 
   logRequest (par.logFileName, date, serverPort, clientIPAddress, postData, userAgent, &clientReq, duration);
   if (userAgent) free (userAgent);
   return true;
}

/*! main server 
  * first argument (mandatory): port number
  * second argument (optionnal): parameter file
  */
int main (int argc, char *argv[]) {
   int serverFd, clientFd;
   struct sockaddr_in address;
   int addrlen = sizeof address;
   int serverPort, opt = 1;
   const double start = monotonic (); 
   signal(SIGPIPE, SIG_IGN);   // Ignore SIGPIPE globally

   if ((bigBuffer =malloc (BIG_BUFFER_SIZE)) == NULL) {
      fprintf (stderr, "In main, Error: Malloc: %d,", BIG_BUFFER_SIZE);
      return EXIT_FAILURE;
   }

   if (setlocale (LC_ALL, "C") == NULL) {                // very important for printf decimal numbers
      fprintf (stderr, "In main, Error: setlocale failed");
      return EXIT_FAILURE;
   }

   if (argc <= 1 || argc > 3) {
      fprintf (stderr, "Synopsys: %s %s\n", argv [0], SYNOPSYS);
      return EXIT_FAILURE;
   }
   
   if (argc > 2)
      g_strlcpy (parameterFileName, argv [2], sizeof parameterFileName);
   else 
      g_strlcpy (parameterFileName, PARAMETERS_FILE, sizeof parameterFileName);

   if (! initContext (parameterFileName, ""))
      return EXIT_FAILURE;

   // option case, launch optionManage
   if (argv[1][0] == '-') {
      optionManage (argv [1][1]);
      return EXIT_SUCCESS;
   }

   // No option, normal case, launch of server
   serverPort = atoi (argv [1]);
   if (serverPort < 80 || serverPort > 9000) {
      fprintf (stderr, "In main, Error: port server not in range\n");
      return EXIT_FAILURE;
   }

   // Socket 
   serverFd = socket (AF_INET, SOCK_STREAM, 0);
   if (serverFd < 0) {
      perror ("In main, Error: socket failed");
      return EXIT_FAILURE;
   }

   // Allow adress reuse juste after closing
   if (setsockopt (serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) < 0) {
      perror ("In main, Error setsockopt");
      close (serverFd);
      return EXIT_FAILURE;
   }

   // Define server parameters address port
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons (serverPort);

   // Bind socket with port
   if (bind (serverFd, (struct sockaddr *)&address, sizeof address) < 0) {
      perror ("In main, Error socket bind"); 
      close (serverFd);
      return EXIT_FAILURE;
   }

   // Listen connexions
   if (listen (serverFd, 3) < 0) {
      perror ("In main: Error listening");
      close (serverFd);
      return EXIT_FAILURE;
   }
   const double elapsed = monotonic () - start; 
   printf ("✅ Loaded in...: %.2lf seconds. Server listen on port: %d, Pid: %d\n", elapsed, serverPort, getpid ());

   while (clientReq.type != REQ_KILL) {
      if ((clientFd = accept (serverFd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
         perror ("In main: Error accept");
         close (serverFd);
         return EXIT_FAILURE;
      }
      handleClient (serverPort, clientFd, &address);
      fflush (stdout);
      fflush (stderr);
      close (clientFd);
   }
   close (serverFd);
   free (tIsSea);
   free (isoDesc);
   free (isocArray);
   free (route.t);
   // freeHistoryRoute ();
   free (tGribData [WIND]); 
   free (tGribData [CURRENT]); 
   free (bigBuffer);
   return EXIT_SUCCESS;
}

