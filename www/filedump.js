/* jshint esversion: 6 */

/**
 * Display raw text file and allow download
 * @param {string} dir
 * @param {string} fileName
 */
function dumpFile(dir, fileName) {
   if (dir !== "") dir = dir + "/";
   const formData = `type=${REQ.DUMP_FILE}&file=${dir}${fileName}`;
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   console.log("Request sent:", formData);

   fetch(apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store"
   })
   .then(response => {
      if (!response.ok) throw new Error(`HTTP error! Status: ${response.status}`);
      return response.text(); // raw text
   })
   .then(text => {
      Swal.fire({
         title: `File: ${fileName}`,
         html: `
            <div style="max-height:400px; overflow:auto; text-align:left;">
               <pre style="white-space:pre;">${esc(text)}</pre>
            </div>
         `,
         width: "80%",
         showDenyButton: true,
         showCancelButton: false,
         confirmButtonText: "Close",
         denyButtonText: "Download",
         footer: `chars: ${text.length.toLocaleString()}`
      }).then(result => {
         if (result.isDenied) {
            downloadTextFile(fileName, text);
         }
      });
   })
   .catch(error => {
      console.error("Error requesting file:", error);
      Swal.fire("Erreur", "Impossible to get file content", "error");
   });
}

/**
 * Download a text file
 * @param {string} filename
 * @param {string} content
 */
function downloadTextFile(filename, content) {
   const blob = new Blob([content], { type: "text/plain;charset=utf-8" });
   const url = URL.createObjectURL(blob);

   const a = document.createElement("a");
   a.href = url;
   a.download = filename;
   document.body.appendChild(a);
   a.click();
   document.body.removeChild(a);

   URL.revokeObjectURL(url); // cleaning
}

/**
 * Choose a file then launch dumpFile
 * @param {string} dir
 */
async function chooseDumpFile (dir = "") {
   /*let options = {};
   options [""] = ".";
   options ["par"] = "par";
   let { value: dir } = await Swal.fire({
      title: 'Model selection',
      input: 'select',
      inputOptions: options,
      inputPlaceholder: 'Choose Dir',
      showCancelButton: true
   });
   if (!dir) dir = "";*/
   const fileName = await chooseFile(dir, "", false, 'Select File'); // wait selection
   if (!fileName) { console.log("No file"); return; }
   dumpFile (dir, fileName);
}



