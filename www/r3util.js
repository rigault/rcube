/**
 * display information about point (lat, lon) of competitor 0
 */
function coordStatus () {
   if (! competitors || ! competitors [0]) return;
   const c = competitors [0];
   const formData = `type=${REQ.COORD}&boat=${c.name},${c.lat},${c.lon};`;
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
      Swal.fire({
         title: "Coord Info",
         html: `
            name: ${c.name}<br>
            coord: ${latLonToStr(c.lat, c.lon)}<br>
            isSea: ${data.isSea}<br>
            isSeaTolerant: ${data.isSeaTolerant}<br>
            inWind: ${data.inWind}<br>
            inCurrent: ${data.inCurrent}
         `,
         icon: "info",
         confirmButtonText: "OK",
         confirmButtonColor: "orange",
         customClass: { popup: "swal-wide" },
      });
   })
   .catch (error => {
      console.error("Error statusCoord:", error);
      Swal.fire("Erreur", "Impossible to access server", "error");
   });
}

/**
 * Set DMS Degree Minute Second Display style
 * update global DMSType variable
 */
function updateDMS() {
  Swal.fire({
    title: "DMS display choice",
    confirmButtonText: "Confirm",
    showCancelButton: true,
    focusConfirm: false,
    input: 'select',
    inputOptions:  ['BASIC', 'DD', 'DM', 'DMS'],
    inputPlaceholder: 'DMS Type'
  }).then((result) => {
    if (result.isConfirmed) {
      console.log("DMS display set to:", result.value);
      DMSType = Number (result.value);
    }
  });
}

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
      //buttonsStyling: false,
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


function helpInfoHtml(data, full) {
  const head = `
    <style>
      .swal-links { color:#444; text-decoration:none; font-weight:bold; }
      .swal-links:hover { text-decoration:underline; color:#222; }
    </style>
    <strong>Rcube:</strong><br>
    <strong>Version:</strong> 1.0.0<br><br>
    <strong>© 2025 rene.rigault@wanadoo.fr</strong><br><br>
  `;

  const bodyFull = `
    <strong>Références :</strong><br>
    <a href="https://www.windy.com/" class="swal-links" target="_blank">Windy</a><br>
    <a href="https://leafletjs.com/" class="swal-links" target="_blank">Leaflet</a><br>
    <strong>from server:</strong><br>
    ${data["Prog-version"]}<br>
    GRIB Reader: ${data["Grib Reader"]}<br>
    GRIB Wind Memory: ${data["Memory for Grib Wind"]}<br>
    GRIB Current Memory: ${data["Memory for Grib Current"]}<br>
    API server port: ${data["API server port"]}<br>
    Memory usage in KB: ${data["Memory usage in KB"]}<br>
    Authorization-Level: ${data["Authorization-Level"]}<br>
    Client IP address: ${data["Client IP Address"]}<br>
    User Agent: ${data["User Agent"]}<br>
    Compilation-date: ${data["Compilation-date"]}<br>
    Client side Windy model: ${store.get('product')}
  `;

  return full ? head + bodyFull : head; // court = seulement l'en-tête
}

/**
 * Display help Info
 * Retrieve some info from server
 */
async function helpInfo (full = false) {
   const formData = `type=${REQ.TEST}`;
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   console.log ("Request sent:", formData);
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
         html:  helpInfoHtml(data, full),
         icon: "info",
         confirmButtonText: "OK",
         confirmButtonColor: "orange",
         showDenyButton: true,
         denyButtonText: full ? "Less" : "More",
         customClass: { popup: "swal-wide" },
      }).then((result) => {
         if (result.isDenied) helpInfo(!full);
      });
   })
   .catch (error => {
      console.error("Error requesting help:", error);
      Swal.fire("Erreur", "Impossible to access server", "error");
   });
}

