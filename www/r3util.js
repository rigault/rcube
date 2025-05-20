
/**
 * Init grib file name from server
 */
function showInitMessage (language = 'fr') {
   const messages = {
      fr: {
         title: "Information",
         text: `Connexion au serveur réussie. <br>
Clic droit pour déplacer le bateau et fixer une destination avec ou sans waypoints.<br>
Menu <b>Route/Launch</b> pour lancer un routage.`,
         button: "🇬🇧 English"
      },
      en: {
         title: "Information",
         text: `The server is online.<br>
Right click to move the boat and to choose a destination with or without waypoints.<br>
Menu <b>Route/Launch</b> to launch route calculation.`,
         button: "🇫🇷 Français"
      }
   };
   Swal.fire({
      title: messages[language].title,
      html: messages[language].text,
      icon: "info",
      showCancelButton: true,
      confirmButtonText: messages[language].button,
      cancelButtonText: "OK",
      customClass: {
         confirmButton: 'swal-init-confirm',
         cancelButton: 'swal-init-cancel'
      }
   }).then((result) => {
      if (result.isConfirmed) {
         showInitMessage(language === 'fr' ? 'en' : 'fr');
      }
   });
}

/// Ajouter les styles CSS pour personnaliser les boutons
const style = document.createElement('style');
style.innerHTML = `
    .swal-init-cancel {
        background-color: orange !important;
        color: white !important;
    }
    .swal-init-confirm {
        background: none !important;
        border: none !important;
        color: inherit !important;
        box-shadow: none !important;
    }
`;
document.head.appendChild(style);

/**
 * Get Grib name from server and display init Info
 */
function getServerInit () {
   const formData = `type=${REQ.PAR_JSON}`;
   console.log ("Request sent:", formData);
   fetch (apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => response.json())
   .then(data => {
      console.log ("JSON received:", data);
      // Dialog box display
      gribLimits.bottomLat = data.bottomLat;
      gribLimits.leftLon = data.leftLon;
      gribLimits.topLat = data.topLat;
      gribLimits.rightLon = data.rightLon;
      gribLimits.name = data.grib;
      drawGribLimits (gribLimits);
      updateStatusBar ();
      showInitMessage ();
   })
   .catch (error => {
      console.error("Error Init:", error);
      Swal.fire("Erreur", "Impossible to access server", "error");
   });
}

/**
 * Display help Info
 * Retrieve some info from server
 */
function helpInfo () {
   const formData = `type=${REQ.TEST}`;
   console.log ("Request sent:", formData);
   fetch (apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => response.json())
   .then(data => {
      console.log ("JSON received:", data);
      // Dialog box display
      Swal.fire({
         title: "Help Info",
         html: `
            <style>
               .swal-links {
                  color: #444; /* Dark gray */
                  text-decoration: none;
                  font-weight: bold;
               }
               .swal-links:hover {
                  text-decoration: underline;
                  color: #222; /* Darker */
               }
         </style>
            <strong>Rcube:</strong><br>
            <strong>Version:</strong> 1.0.0<br><br>
            <strong>© 2025 rene.rigault@wanadoo.fr</strong><br><br>
            <strong>Références :</strong><br>
            <a href="https://www.windy.com/" class="swal-links" target="_blank">Windy</a><br>
            <a href="https://leafletjs.com/" class="swal-links" target="_blank">Leaflet</a><br>
            <a href="https://confluence.ecmwf.int/display/ECC/" class="swal-links" target="_blank">ECMWF Eccodes</a><br>
            <strong>from server:</strong><b>
            ${data ["Prog-version"]}<br>
            GLIB version: ${data ["GLIB-version"]}<br>
            ECCODES-version: ${data ["ECCODES-version"]}<br>
            CURL-version: ${data ["CURL-version"]}<br>
            API server port:  ${data ["API server port"]}<br>
            Memory usage in KB:  ${data ["Memory usage in KB"]}<br>
         `,
         icon: "info",
         confirmButtonText: "OK",
         confirmButtonColor: "orange",
         customClass: { popup: "swal-wide" },
      });
   })
   .catch (error => {
      console.error("Error requesting help:", error);
      Swal.fire("Erreur", "Impossible to access server", "error");
   });
}

