/*! compilation: gcc -c mailutil.c `pkg-config --cflags glib-2.0` */
#define _POSIX_C_SOURCE 200809L // to avoid warning with -std=c11 when using popen function (Posix)
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <glib.h>
#include "r3types.h"

#define TEMP_FETCH          "tempfetch.tmp"
#define MAX_N_ERROR_MESSAGE 10

extern Par par;

struct uploadStatus {
   const char *data;
   size_t bytes_left;
};

/*! replace $ by sequence */
char *dollarSubstitute (const char* str, char *res, size_t maxLen) {
#ifdef _WIN32
   g_strlcpy (res, str, maxLen); // no issue with $ with Windows 
   return res;
#else  
   res [0] = '\0';
   if (str == NULL) return NULL;
   int len = 0;
   for (size_t i = 0; (i < maxLen) && (str[i] != '\0'); ++i) {
      if (str[i] == '$') {
         res[len++] = '\\';
         res[len++] = '$';
      } else {
         res[len++] = str[i];
      }
   }
   res[len] = '\0';
   return res;
#endif
}

/*! callback function to send email content */
static size_t payloadSource (void *ptr, size_t size, size_t nmemb, void *userp) {
   struct uploadStatus *upload_ctx = (struct uploadStatus *) userp;
   size_t buffer_size = size * nmemb;

   if (upload_ctx->bytes_left) {
       size_t copy_size = buffer_size < upload_ctx->bytes_left ? buffer_size : upload_ctx->bytes_left;
       memcpy(ptr, upload_ctx->data, copy_size);
       upload_ctx->data += copy_size;
       upload_ctx->bytes_left -= copy_size;
       return copy_size;
   }
   return 0;
}

/*! send using smtp mail with object and messsage to toAddress with python script */
static bool smtpSendPython (const char *toAddress, const char *object, const char *message) {
   char commandLine [MAX_SIZE_LINE * 4];
   char newPw [MAX_SIZE_NAME]; // only for Python case 

   dollarSubstitute (par.mailPw, newPw, MAX_SIZE_NAME);

   snprintf (commandLine, sizeof (commandLine), "%s %s \"%s\" \"%s\" %s\n", 
      par.smtpScript, toAddress, object, message, newPw);
   
   if (system (commandLine) != 0) {
      fprintf (stderr, "In smtpSendPython: system call Error: %s\n", commandLine);
      return false;
   }

   char *pt = strrchr (commandLine, ' '); // do not print password
   *pt = '\0';
   printf ("End command: %s\n", commandLine);
   return true;
}

/*! Replace \n with \r\n */
char *normalizeNewlines (const char *message) {
   GRegex *regex = g_regex_new("\n", 0, 0, NULL);
   char *normalized = g_regex_replace_literal(regex, message, -1, 0, "\r\n", 0, NULL);
   g_regex_unref(regex);
   return normalized; 
}


/*! send using smtp mail with object and messsage to toAddress */
bool smtpSend (const char *toAddress, const char *object, const char *message) {
   CURL *curl;
   CURLcode res;
   struct curl_slist *recipients = NULL;
   if (par.python)
      return smtpSendPython (toAddress, object, message);

   printf ("in Smtp: %s\n", message);

   char *normalizedMessage = normalizeNewlines(message);
   if (! normalizedMessage) {
      return false;
   }

   // email content building
   size_t payload_size = strlen(toAddress) + strlen(par.smtpUserName) + strlen(object) + strlen(normalizedMessage) + 256;
   char *payload = g_malloc(payload_size);
   if (payload == NULL) {
      fprintf (stderr, "In smtpSend: Error malloc\n");
      return false;
   }

   snprintf(payload, payload_size,
         "To: %s\r\n"
         "From: %s\r\n"
         "Subject: %s\r\n"
         "\r\n"
         "%s\r\n",
         toAddress, par.smtpUserName, object, normalizedMessage);

   g_free(normalizedMessage);

   struct uploadStatus upload_ctx = {payload, strlen(payload)};

   // CURL init
   curl = curl_easy_init();
   if (!curl) {
       fprintf (stderr, "In smtpSend: Error in CURL init\n");
       free (payload);
       return false;
   }

   // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // debug

   // CURL Configuration for SMTP with SSL/TLS
   curl_easy_setopt (curl, CURLOPT_URL, par.smtpServer);
   curl_easy_setopt (curl, CURLOPT_USERNAME, par.smtpUserName);
   curl_easy_setopt (curl, CURLOPT_PASSWORD, par.mailPw);
   curl_easy_setopt (curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
   curl_easy_setopt (curl, CURLOPT_MAIL_FROM, par.smtpUserName);

   // recipent add
   recipients = curl_slist_append (recipients, toAddress);
   curl_easy_setopt (curl, CURLOPT_MAIL_RCPT, recipients);

   // Define e-mail content
   curl_easy_setopt (curl, CURLOPT_READFUNCTION, payloadSource);
   curl_easy_setopt (curl, CURLOPT_READDATA, &upload_ctx);
   curl_easy_setopt (curl, CURLOPT_UPLOAD, 1L);

   // send e-mail
   res = curl_easy_perform (curl);

   if (res != CURLE_OK) {
       fprintf (stderr, "In smtpSend: Error sending email: %s\n", curl_easy_strerror(res));
   } else {
       printf ("In smtpSend: email sent to: %s object: %s body: %s\n", toAddress, object, message);
   }

   // cleaning
   curl_slist_free_all (recipients);
   curl_easy_cleanup (curl);
   free (payload);

   return res == CURLE_OK;
}

/*! mark all messages of mailbox as read 
   return false if no message marked, true if at least one marked */
static bool markAsReadPython () {
   char command [MAX_SIZE_LINE * 2] = "";
   char newPw [MAX_SIZE_NAME];
   dollarSubstitute (par.mailPw, newPw, MAX_SIZE_NAME);
   snprintf (command, sizeof (command), "%s %s", par.imapToSeen, newPw);
   if (system (command) != 0) {
      fprintf (stderr, "In markAsReadPython. Error in system command: %s\n", command);
      return false;
   }
   return true;
}

/*! mark all messages of mailbox as read 
   return false if no message marked, true if at least one marked */
bool markAsRead (const char *imapServer, const char *username, const char *password, const char *mailbox) {
   if (par.python)
      return markAsReadPython ();
   CURL *curl;
   CURLcode res;
   char url [MAX_SIZE_URL];

   curl = curl_easy_init();
   if (! curl) {
      fprintf (stderr, "In markAsRead. Error in curl initcurl\n");
      return false;
   }

   snprintf (url, sizeof(url), "imaps://%s/%s", imapServer, mailbox);

   curl_easy_setopt(curl, CURLOPT_URL, url);
   curl_easy_setopt(curl, CURLOPT_USERNAME, username);
   curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
   curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mailbox);
   curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "STORE 1:* +FLAGS \\Seen"); // Mark all messages read

   // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // debug
   res = curl_easy_perform(curl);
   //if (res != CURLE_OK) fprintf (stderr, "In markAsRead. curl_easy_perform() failed: %s\n", curl_easy_strerror (res));
   curl_easy_cleanup (curl);
   return (res == CURLE_OK);
}

/*! Extract text (n first lines after "Error" word) from temp filename */
static void writeErrorMessage (const char *filename, int n) {
   int count = 0;
   char line [MAX_SIZE_LINE];
   FILE *file = fopen (filename, "r");
   if (file == NULL) {
      fprintf (stderr, "In extractFilename: Error opening file: %s\n", filename);
      return;
   }
   while (fgets(line, sizeof (line), file) && count < n) {
      char *lowerLine = g_ascii_strdown (line, -1);
      if (strstr (lowerLine, "error")) {
         count = 1;
      }
      if (count != 0) {
         printf ("%s", line);
         count += 1;
      }
      g_free (lowerLine);
   }
   fclose (file);
}

/*! Extract grib file name from text file filename */
static char* extractFilename (const char *filename) {
   FILE *file = fopen (filename, "r");
   char line [MAX_SIZE_LINE];
   char *filenameFound = NULL;
   if (file == NULL) {
      fprintf (stderr, "In extractFilename: Error opening file: %s\n", filename);
      return NULL;
   }

   while (fgets (line, sizeof (line), file)) {
      // check Content-Disposition ou Content-Type with "filename=" or "name="
      char *lowerLine = g_ascii_strdown (line, -1);

      if (strstr(lowerLine, "content-disposition:") || strstr(lowerLine, "content-type:")) {
         char *start = strstr(line, "name="); // match both filename= and name=
         if (start) {
            start += 5;                     // ignore "filename=" or "name="
            if (*start == '"') start++;     // Ignore "
            char *end = strchr(start, '"'); // search final "
            if (end) {
               *end = '\0'; // Terminate
            }
            filenameFound = g_strdup (start); // Copy filename
            break;
         }
      }
      g_free (lowerLine);
   }
   fclose(file);
   if (!filenameFound) {
      fprintf (stderr, "In extractFilename: grib file name not found\n");
   }
   return filenameFound;
}

/*! extract Base64 content from text filename */
static char *extractBase64Content (const char *filename) {
   FILE *file = fopen(filename, "r");
   if (file == NULL) {
      fprintf (stderr, "In ExtractBase64Content: Error opening file: %s", filename);
      return NULL;
   }
   GString *contentBuffer = g_string_new (NULL); // dynamic buffer
   bool isBase64Section = false;
   char line [MAX_SIZE_LINE_BASE64];

   while (fgets(line, sizeof(line), file)) {
      // find beginning of base64 section
      char *lowerLine = g_ascii_strdown (line, -1);
      if (!isBase64Section &&  (strstr(lowerLine, "content-transfer-encoding: base64") != NULL)) {
         isBase64Section = true;
         continue; // Passer à la ligne suivante
      }
      // collect encoded lines
      if (isBase64Section) {
         if ((strstr (lowerLine, "content-") != NULL) && (strstr (lowerLine, "name="))) continue; // next line 
         // stop when MIME delimitor or new section found
         if (line[0] == '-' || strstr (lowerLine, "content-") != NULL) {
            break;
         }
         g_string_append (contentBuffer, g_strstrip(line));
      }
      g_free (lowerLine);
   }
   fclose(file);
   if (contentBuffer->len == 0) {
      fprintf (stderr, "In extractBase64Content: no data found in file: %s\n", filename);
      g_string_free (contentBuffer, TRUE);
      return NULL;
   }
   char *base64_content = g_strdup (contentBuffer->str);
   g_string_free (contentBuffer, TRUE);
   return base64_content;
}

/*! return first integer found in str */
static int extractFirstInteger (const char *str) {
   const char *p = str;
   while (*p) {
      if (isdigit(*p)) {
         int num = 0;
         while (isdigit(*p)) {
            num = num * 10 + (*p - '0');
            p++;
         }
         return num; 
      }
      p++;
   }
   return -1;  // No integer found
}

/*! Callback function to write in a file */
static size_t writeToFile (void *ptr, size_t size, size_t nmemb, FILE *stream) {
   return fwrite (ptr, size, nmemb, stream);
}

/*! read imapbox, check if unseen message. Save first (oldest) content of message in a file */
static int imapRead (const char* imapServer, const char *username, const char *password, const char *mailbox, const char *tempFileName) {
   CURL *curl;
   CURLcode res;
   int firstUnSeen = 0;;
   char response [MAX_SIZE_BUFFER] = {0}; // store response for message list

   char url [MAX_SIZE_URL];
   FILE *fileFetch = fopen (tempFileName, "w");
   if (fileFetch == NULL) {
      fprintf (stderr, "In imapRead, Error creating file: %s\n", tempFileName);
      return -1;
   }

   // first step: find first unseen message (the oldest)
   snprintf (url, sizeof(url), "imaps://%s/%s", imapServer, mailbox);
    
   curl_global_init (CURL_GLOBAL_DEFAULT);
   curl = curl_easy_init();

   if (!curl) {
      fprintf (stderr, "In imapRead, Failed to initialize libcurl\n");
      fclose (fileFetch);
      return -1;
   }
   curl_easy_setopt (curl, CURLOPT_URL, url);
   curl_easy_setopt (curl, CURLOPT_USERNAME, username);
   curl_easy_setopt (curl, CURLOPT_PASSWORD, password);
   // IMAP command `SEARCH UNSEEN`
   curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "SEARCH UNSEEN");
   // redirect response in buffer
   FILE *memfile = fmemopen (response, MAX_SIZE_BUFFER, "w");
   if (memfile == NULL) {
      fprintf (stderr, "In imapRead: Failed to open memory file\n");
      curl_easy_cleanup(curl);
      fclose (fileFetch);
      return -1;
   }
   curl_easy_setopt (curl, CURLOPT_WRITEDATA, memfile);

   res = curl_easy_perform (curl);
   fclose (memfile);

   if (res != CURLE_OK) {
      fprintf (stderr, "In imapRead, search unseen; curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
   } else {
      // Check if unread messages exist
      // printf ("In lastMessageUnseen: %s\n", response);
      if (strstr(response, "* SEARCH") != NULL) {
         firstUnSeen = extractFirstInteger (response);
         // printf ("Message Unseen has number:%d\n",  firstUnSeen);
      }
   }
   if (firstUnSeen < 1) {
      // fprintf (stderr, "No unseen messages\n");
      curl_easy_cleanup(curl);
      fclose (fileFetch);
      return -1;
   }

   curl_easy_cleanup(curl);
   curl_global_cleanup();

   // second step: find message with number firstUnSeen
   curl_global_init (CURL_GLOBAL_DEFAULT);
   curl = curl_easy_init();
   if (!curl) {
      fprintf (stderr, "In imapRead, failed to initialize libcurl\n");
      fclose (fileFetch);
      return -1;
   }
   
   snprintf (url , sizeof(url), "imaps://%s/%s;MAILINDEX=%d", imapServer, mailbox, firstUnSeen);
   curl_easy_setopt(curl, CURLOPT_URL, url);
   curl_easy_setopt (curl, CURLOPT_USERNAME, username);
   curl_easy_setopt (curl, CURLOPT_PASSWORD, password);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, fileFetch);
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
   res = curl_easy_perform (curl);

   if (res != CURLE_OK)
     fprintf (stderr, "In imapRead, fetching: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
   //else printf ("OK\n");

   fclose (fileFetch);
   curl_global_cleanup();
   return (firstUnSeen);
}

/*! python version for imapGetUnseen */
static int imapGetUnseenPython (const char *path, char *gribFileName, size_t maxLen) {
   char *fileName;
   const int maxLines = 10;
   char line [MAX_SIZE_LINE] = "";
   char buffer[MAX_SIZE_BUFFER] = "\n";
   int n = 0;
   static int count; 
   FILE *fp;
   char command [MAX_SIZE_LINE *2] = "";
   char newPw [MAX_SIZE_NAME];
   char *end;

   dollarSubstitute (par.mailPw, newPw, MAX_SIZE_NAME);
   snprintf (command, sizeof (command), "%s %s %s", par.imapScript, path, newPw); 
   if ((fp = popen (command, "r")) == NULL) {
      fprintf (stderr, "In imapGetUnseenPython, Error popen command: %s\n", command);
      return 0;
   }
   while ((fgets (line, sizeof(line)-1, fp) != NULL) && (n < maxLines)) {
      n += 1;
      printf ("count: %d line: %d: %s", count, n, line);
      g_strlcat (buffer, line, sizeof (buffer));
   }
   count += 1;
   pclose(fp);
   if (n > 1) {
      // printf ("imapGetUnseenPython\n");
      if (strstr (buffer, "Email size limit exceeded") != NULL) {
         return -1;
      }
      else if (((fileName = strstr (buffer, "File: ") + 6) != NULL) && (n > 2)) {
         printf ("Mail Response: %s\n", buffer);
         while (isspace (*fileName)) fileName += 1;
         if ((end = strstr (fileName, " ")) != NULL)
            *(end) = '\0';
         printf ("fileName found:%s\n", fileName);
         g_strlcpy (gribFileName, fileName, maxLen);
         return 1; // OK mail found
      }
      else {
         return 0;
      }
   }
   else return -1; // Still running, no error bu no mail
}

/*! read mailbox, check unseen message. If exist, the grib file is decoded and saved in directory path 
   return 0 : error
   return -1 : normal but no unseen message found
   return 1 : unseen message found and decoded 
   name of grib file found returned in gribFileName */
int imapGetUnseen (const char* imapServer, const char *username, 
   const char* password, const char *mailbox, const char *path, char *gribFileName, size_t maxLen) {
   if (par.python) {
      return imapGetUnseenPython (path, gribFileName, maxLen);
   }
   // Step1: find the unread message in mailbox and produce temporary file
   char tempFileName [MAX_SIZE_FILE_NAME];

   snprintf (tempFileName, sizeof (tempFileName), "%s/%s", path, TEMP_FETCH);
   //printf ("tempFileName: %s\n", tempFileName);
   int res = imapRead (imapServer, username, password, mailbox, tempFileName);
   if (res < 1) return -1; // normal return. No error but no message found

   // Step2: extract file name from temporary file
   char *extractedName = extractFilename (tempFileName);
   if (extractedName) {
      printf("Extracted Grib File Name: %s\n", extractedName);
   }
   else {
      writeErrorMessage (tempFileName, MAX_N_ERROR_MESSAGE);
      g_free (extractedName);
      return 0;          // Error: Message found with no gribfile name inside
   }
   snprintf (gribFileName, maxLen, "%s/%s", path, extractedName);
   // printf("Total Grib File Name: %s\n", gribFileName);

   // Step 3: extract base64 encoded content from temporary file
   char *base64_content = extractBase64Content (tempFileName);
   if (!base64_content) {
      writeErrorMessage (tempFileName, MAX_N_ERROR_MESSAGE);
      fprintf (stderr, "In imapGetUnseen, impossible to extract encoded content.\n");
      g_free (extractedName);
      return 0;          // Error: Message found but not decodable
   }

   // Step 4: Decode Base64 content
   gsize decoded_length;
   guchar *decoded_data = g_base64_decode (base64_content, &decoded_length);
   g_free (base64_content);

   if (!decoded_data) {
      fprintf (stderr, "In impapGetUnseen, error decoding Base64.\n");
      g_free (extractedName);
      return 0;           // Error
   }

   // Step 5: Write decoded data in file
   FILE *gribFile = fopen (gribFileName, "wb");
   if (gribFile == NULL) {
      fprintf (stderr, "In impaGetUnseen, error creating output grib file: %s\n", gribFileName);
      g_free (decoded_data);
      g_free (extractedName);
      return 0;            // Error
   }

   fwrite (decoded_data, 1, decoded_length, gribFile);
   fclose (gribFile);
   g_free (decoded_data);

   printf ("Success ! grib file is: %s\n", gribFileName);
   g_free (extractedName);

   return 1;
}

/*! curGet download URL and save the content in outputFile using system command */
static bool curlGetSys(const char *url, const char *outputFile, char *errMessage, size_t maxLen) {
   char command [MAX_SIZE_STD];
   if (snprintf(command, sizeof(command), "curl -L --fail -o %s \"%s\" 2>/dev/null", outputFile, url) >= (int)sizeof(command)) {
      snprintf(errMessage, maxLen, "Command too long");
      return false;
   }
   int ret = system (command); // execute the commande
   if (ret == -1) {
      snprintf (errMessage, maxLen, "In curlGetSys: System call failed: %s", strerror(errno));
      return false;
   }

   if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) { // Check the return value of the curl command
      return true;
   } else {
      if (WIFEXITED(ret)) {
         snprintf (errMessage, maxLen, "In curlGetSys: Curl failed with exit code: %d", WEXITSTATUS(ret));
      } else {
         snprintf (errMessage, maxLen, "In curlGetSys: Curl terminated abnormally");
      }
      return false;
   }
   printf ("File downloaded with system curl: %s\n", outputFile);
}

/*! curGet download URL and save the content in outputFile */
bool curlGet (const char *url, const char *outputFile, char *errMessage, size_t maxLen) {
   if (par.curlSys)
      return curlGetSys (url, outputFile, errMessage, maxLen);

   if (url == NULL || outputFile == NULL || errMessage == NULL || maxLen == 0) {
      g_strlcpy (errMessage, "Invalid arguments", maxLen);
      return false;
   }
   long http_code = 0;
   CURL *curl = curl_easy_init();
   if (curl == NULL) {
      return false;
   }
   curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
   curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 2L); // Temps min. en secondes avant un timeout
   curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L); // Par exemple 100 KB

   // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Verbose activated

   FILE *f = fopen(outputFile, "wb");
   if (f == NULL) {
      snprintf(errMessage, maxLen, "Error opening file: %s", outputFile);
      curl_easy_cleanup(curl);
      return false;
   }

   curl_easy_setopt (curl, CURLOPT_URL, url);
   curl_easy_setopt (curl, CURLOPT_WRITEDATA, f); // no explicit WRITEFUNCTION 

   // blocking mode activated
   curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
   curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

   CURLcode res = curl_easy_perform(curl);
   fflush(f); // Assure
   fclose(f);

   if (res != CURLE_OK) {
      snprintf(errMessage, maxLen, "Curl error: %s", curl_easy_strerror(res));
      curl_easy_cleanup(curl);
      return false;
   }

   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
   curl_easy_cleanup (curl);

   if (http_code >= 400) {
      snprintf(errMessage, maxLen, "HTTP error: %ld", http_code);
      return false;
   }
   return true;
}

/*! check server accessibility */
bool isServerAccessible (const char *url) {
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init ();

    if (curl) {
        curl_easy_setopt (curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        res = curl_easy_perform (curl);
        if (res != CURLE_OK) {
            fprintf (stderr, "In isServerAccessible, Error Curl request: %s\n", curl_easy_strerror(res));
            curl_easy_cleanup (curl);
            return false; // server not accessible request error
        }

        long http_code;
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

        // 2xx means server accessible
        if (http_code >= 200 && http_code < 300) {
            curl_easy_cleanup(curl);
            return true;
        } else {
            curl_easy_cleanup(curl);
            return false; 
        }
    } else {
        fprintf (stderr, "In isServerAccessible, Error Init curl.\n");
        return false;
    }
}


