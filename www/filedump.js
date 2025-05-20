/* jshint esversion: 6 */

/**
 * Format file size
 * @param {number} bytes
 * @returns {string}
 */
function formatFileSize(bytes) {
   if (bytes >= 1_000_000) {
      return (bytes / 1_000_000).toFixed(1) + " MB";
   } else if (bytes >= 1_000) {
      return (bytes / 1_000).toFixed(1) + " KB";
   } else {
      return bytes + " bytes";
   }
}

/**
 * Escape HTML special characters to safely display text inside HTML
 * @param {string} unsafe
 * @returns {string}
 */
function escapeHtml(unsafe) {
   return unsafe
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
}

/**
 * Display raw text file and allow download
 * @param {string} dir
 * @param {string} fileName
 */
function dumpFile(dir, fileName) {
   if (dir !== "") dir = dir + "/";
   const formData = `type=${REQ.DUMP_FILE}&file=${dir}${fileName}`;
   console.log("Request sent:", formData);

   fetch(apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => {
      if (!response.ok) {
         throw new Error(`HTTP error! Status: ${response.status}`);
      }
      return response.text(); // ATTENTION ici c'est .text() car c'est du texte brut !
   })
   .then(text => {
      let parsed;
      try {
         parsed = JSON.parse(text);
      } catch (e) {
         parsed = null;
      }

      if (parsed && parsed._Error) {
         Swal.fire("Error", parsed._Error, "error");
         return;
      }

      Swal.fire({
         title: `File: ${fileName}`,
         html: `
            <div style="max-height:400px; overflow:auto; text-align:left;">
               <pre style="white-space:pre;">${escapeHtml(text)}</pre>
            </div>
         `,
         width: "80%",
         showDenyButton: true,
         showCancelButton: false,
         confirmButtonText: "Close",
         denyButtonText: "Download",
         //cancelButtonText: "Cancel"
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

   URL.revokeObjectURL(url); // Nettoyage
}


/**
 * Choose a file then launch dumpFile
 * @param {string} dir
 */
function chooseFile (dir = "") {
   const formData = `type=${REQ.DIR}&dir=${encodeURIComponent(dir)}`;
   console.log("Request sent:", formData);

   fetch(apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => {
      if (!response.ok) {
         throw new Error(`HTTP error! Status: ${response.status}`);
      }
      return response.json();
   })
   .then(data => {
      if (!Array.isArray(data) || data.length === 0) {
         Swal.fire("Error", "No file found", "error");
         return;
      }

      // Création du menu déroulant AVEC la taille et la date
      const fileOptions = data.map(file => {
         const fileName = file[0];
         const fileSize = formatFileSize(file[1]);
         const fileDate = file[2];
         return `<option value="${fileName}">${fileName} (${fileSize} - ${fileDate})</option>`;
      }).join("");

      // Dialog box display
      Swal.fire({
         title: "Select a File",
         html: `
            <div class="swal-wide">
               <select id="fileSelect" class="swal2-select">
                  ${fileOptions}
               </select>
            </div>
         `,
         showCancelButton: true,
         confirmButtonText: "Confirm",
         cancelButtonText: "Cancel",
         customClass: { popup: "swal-wide" },
         preConfirm: () => {
            const selectedFile = document.getElementById("fileSelect").value;
            if (!selectedFile) {
               Swal.showValidationMessage("Select a file.");
            }
            return selectedFile;
         }
      }).then(result => {
         if (result.isConfirmed) {
            dumpFile (dir, result.value);
         }
      });
   })
   .catch(error => {
      console.error("Error requesting file:", error);
      Swal.fire("Erreur", "Impossible to get file list", "error");
   });
}
