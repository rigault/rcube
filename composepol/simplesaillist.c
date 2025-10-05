/*! list sails
   Compilation: gcc -o simplesaillist simplesaillist.c `pkg-config --cflags --libs json-glib-1.0 glib-2.0`
*/

#include <stdbool.h>
#include <glib.h>
#include <stdio.h>
#define MAX_N_SAIL         100
#define MAX_SIZE_NAME      64

int simpleSailList (const char *fileName, char sailName [MAX_N_SAIL][MAX_SIZE_NAME], size_t maxSailName) {
   gchar *content = NULL;
   size_t count = 0;
   gsize length = 0;
   GError *error = NULL;
   char *pt, *ptEnd;

   if (! g_file_get_contents (fileName, &content, &length, &error)) {
      g_printerr ("Error: %s\n", error->message);
      g_clear_error (&error);
      return 0;
   }
   pt = content;
   while (((pt = strstr (pt, "name\":")) != NULL) && (count < maxSailName)) {
      pt += 7;
      if ((ptEnd = strchr (pt, ',')) != NULL)
         g_strlcpy (sailName [count++], pt, ptEnd - pt);
   }
   g_free (content);
   return count;
}

int main (int argc, char *argv []) {
   char sailName [MAX_N_SAIL][MAX_SIZE_NAME];
   if (argc != 2) {
     printf ("Usage: %s <json file>\n", argv [0]);
     exit (EXIT_FAILURE);
   }
   int nSail = simpleSailList (argv [1], sailName, MAX_N_SAIL);
   for (int i = 0; i < nSail; i += 1)
      printf ("%s\n", sailName [i]);
}
