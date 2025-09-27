/**
 * SweetAlert2 prompt for credentials
 */
async function promptForCreds() {
   // simple HTML escape for values put into attributes
   const esc = s => (s ?? "").replace(/[&<>"']/g, m => ({
      "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"
   }[m]));
   const html = `
      <form> 
      <input id="swal-user" class="swal2-input" placeholder="User ID" autocomplete="${userId}" value="${esc(userId)}">
      <input id="swal-pass" class="swal2-input" type="password" placeholder="Password" autocomplete="${password}" value="${esc(password)}">
      <!-- <label style="display:flex;align-items:center;gap:.5rem;margin:.25rem 1.25rem 0;">
         <input id="swal-show" type="checkbox"> Show password
      </label> -->
      </form>
   `;

   const res = await Swal.fire({
      title: "Sign in",
      html,
      focusConfirm: false,
      showDenyButton: true,
      showCancelButton: true,
      confirmButtonText: "Sign in",
      denyButtonText: "Continue as guest",
      cancelButtonText: "Cancel",
      didOpen: () => {
         const $popup = Swal.getPopup();
         const $user = $popup.querySelector("#swal-user");
         const $pass = $popup.querySelector("#swal-pass");
         //const $show = $popup.querySelector("#swal-show");
         $user && $user.focus();
         //$show && $show.addEventListener("change", () => {
         //   $pass.type = $show.checked ? "text" : "password";
         //});
      },
      preConfirm: () => {
         const $popup = Swal.getPopup();
         const user = $popup.querySelector("#swal-user").value.trim();
         const pass = $popup.querySelector("#swal-pass").value;
         if (!user || !pass) {
            Swal.showValidationMessage("User ID and password are required to sign in.");
            return false;
         }
         return { user, pass };
      }
   });

   if (res.isConfirmed && res.value) {
      userId = res.value.user;
      password = res.value.pass;
      return "signed-in";
   }
   if (res.isDenied) {
      userId = "anonymous";
      password = "anonymous";
      return "guest";
   }
   return "cancelled";
   // throw new Error("cancelled");
}

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
       remove(filepath border: none !important;
        color: inherit !important;
        box-shadow: none !important;
    }
`;
document.head.appendChild(style);

/**
 * Get Grib name from server, fit map in grib bounds and display init Info
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
      updateStatusBar ();
      showInitMessage ();
      const bounds = [[gribLimits.bottomLat, gribLimits.leftLon],[gribLimits.topLat, gribLimits.rightLon]];
      map.fitBounds(bounds);
      // alert (`bottomLat: ${gribLimits.bottomLat}, leftLon: ${gribLimits.leftLon}, topLat: ${gribLimits.topLat}, rigthtLon: ${gribLimits.rightLon}`);
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
async function helpInfo () {
   const formData = `type=${REQ.TEST}`;
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   const token = btoa(`${userId}:${password}`);
   const auth = `Basic ${token}`;
   console.log ("Request sent:", formData);
   if (auth && userId !== "") headers.Authorization = auth;     // else stay anonymous level 0
   fetch (apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store"
   })
   .then(response => response.json())
   .then(data => {
      console.log (JSON.stringify(data));
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
            <strong>from server:</strong><b>
            ${data ["Prog-version"]}<br>
            GRIB Reader: ${data ["Grib Reader"]}<br>
            GRIB Wind Memory: ${data ["Memory for Grib Wind"]}<br>
            GRIB Current Memory: ${data ["Memory for Grib Current"]}<br>
            API server port:  ${data ["API server port"]}<br>
            Memory usage in KB:  ${data ["Memory usage in KB"]}<br>
            Authorization-Level:  ${data ["Authorization-Level"]}<br>
            Compilation-date: ${data ["Compilation-date"]}<br>
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

