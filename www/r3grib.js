/* jshint esversion: 6 */

const models = ["GFS", "ECMWF", "ARPEGE", "AROME", "UCMC", "SYN"];
let dataGrib = {};

/**
 * Loads GRIB wind data from the server using a POST request, retrieves the
 * binary response (u, v, g, w components), and returns utilities to access the data.
 *
 * The server returns a variable number of components per grid point depending
 * on the `X-Shortnames` HTTP header. Supported combinations are:
 *   - "uv"      → 2 components
 *   - "uvg"     → 3 components
 *   - "uvw"     → 3 components
 *   - "uvgw"    → 4 components
 *
 * The binary payload is decoded into a Float32Array in the following fixed order:
 *   u, v, (g if present), (w if present)
 *
 * @async
 * @function gribLoad
 *
 * @param {string} dir
 *        Directory containing the GRIB file (ignored if `model` is provided).
 *
 * @param {string|null} model
 *        Weather model identifier (e.g., "GFS"), or `null` to load a GRIB file from disk.
 *
 * @param {string} gribName
 *        Name of the GRIB file (used only when `model === null`).
 *
 * @param {number} nTime
 *        Number of time steps in the GRIB dataset.
 *
 * @param {number} nLat
 *        Number of latitude points in the grid.
 *
 * @param {number} nLon
 *        Number of longitude points in the grid.
 *
 * @returns {Promise<{
 *    values: Float32Array,
 *    getUVGW: function(number,number,number): {u:number, v:number, g:number, w:number},
 *    indexOf: function(number,number,number): number,
 *    nLat: number,
 *    nLon: number,
 *    nTime: number,
 *    nShortName: number,
 *    shortnames: string
 * }>}
 *    Resolves to an object containing:
 *    - **values**: the raw Float32Array of decoded binary GRIB data.
 *    - **getUVGW(t, iLat, iLon)**: retrieves {u, v, g, w} at a given grid position.
 *    - **indexOf(t, iLat, iLon)**: returns the base index in the Float32Array.
 *    - **nLat**, **nLon**, **nTime**: grid dimensions.
 *    - **nShortName**: number of components per point (2, 3, or 4).
 *    - **shortnames**: exact shortname string from the HTTP header.
 *
 * @throws {Error}
 *    If the HTTP request fails or returns a non-200 status.
 */

async function gribLoad(dir, model, gribName, nTime, nLat, nLon, nName) {
   const gribParam = model ? `model=${model}` : `grib=${dir}/${gribName}`;
   const formData = `type=${REQ.GRIB_DUMP}&${gribParam}`;
   console.log("Request sent:", formData);

   const headers = {
      "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
   };

   const res = await fetch(apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store",
   });

   if (!res.ok) {
      throw new Error(`gribLoad HTTP error ${res.status}`);
   }

   // get shortnames from HTTP header. ex: "uv", "uvg", "uvw", "uvgw"
   let shortnamesStr = res.headers.get("X-Shortnames");

   if (!shortnamesStr) return;

   const hasU = shortnamesStr.includes("u");
   const hasV = shortnamesStr.includes("v");
   const hasG = shortnamesStr.includes("g");
   const hasW = shortnamesStr.includes("w");

   if (!hasU || !hasV) {
      console.warn("X-Shortnames does not contain both 'u' and 'v'!", shortnamesStr);
   }

   // Number of values per point. Allways uv, optionnaly g/w
   let nShortName = 2;
   if (hasG) nShortName++;
   if (hasW) nShortName++;

   if (nName !== nShortName) {
      console.warn("gribLoad: unexpected nShortName", " Found =", nShortName, "expected =", nName);
      return;
   }

   // Retrive binary datas
   const buf = await res.arrayBuffer();
   const values = new Float32Array(buf);

   // Check consistency of length between parameters and length of file received.
   const expected = nTime * nLat * nLon * nShortName;
   if (values.length !== expected) {
      console.warn("gribLoad: unexpected size", "values.length =", values.length, "expected =", expected);
      return;
   }
   console.log("Shortnames: ", shortnamesStr, ", nShortName: ", nShortName, ", nValues: ", expected);


   // access functions 
   function indexOf(tIndex, iLat, iLon) {
      // même ordre que côté C : t -> lat -> lon -> [u, v, (g), (w)]
      return (((tIndex * nLat) + iLat) * nLon + iLon) * nShortName;
   }

   function getUVGW(tIndex, iLat, iLon) {
      const idx = indexOf(tIndex, iLat, iLon);
      const u = values[idx];
      const v = values[idx + 1];

      let g = 0; //NaN;
      let w = 0; //NaN;
      let offset = 2;

      if (hasG) {
         g = values[idx + offset];
         offset += 1;
      }
      if (hasW) {
         w = values[idx + offset];
      }
      return { u, v, g, w };
   }
   return { values, getUVGW, indexOf, nLat, nLon, nTime, nShortName, shortnames: shortnamesStr };
}


/**
 * Condense list of time stamps in concise way 
 * @param {number[]} timeStamps
 * @returns {string}
 */
function condenseTimeStamps (timeStamps) {
   let result = [];
   if (!timeStamps || timeStamps.length === 0) return "[]";
   if (timeStamps.length < 5) {
      for (let i = 0; i < timeStamps.length; i++) {
         result.push (timeStamps [i]);
      }
      return "[" + result.join(", ") + "]";
   }

   let start = timeStamps[0];
   let prev = start;
   let timeStep = null;
   let diff = null;
   let afterStart = timeStamps [1];
   for (let i = 1; i < timeStamps.length; i++) {
      diff = timeStamps[i] - prev;

      if (timeStep === null) {
         timeStep = diff; // Initialize first time step
      }

      if (diff !== timeStep) {
         // New sequence found
         if (prev !== start) {
            result.push(start + (prev !== start + timeStep ? ", " + afterStart + ".." + prev : ""));
            afterStart = start + diff;
         } else {
            result.push(start);
         }
         start = timeStamps[i];
         timeStep = diff;
      }

      prev = timeStamps[i];
   }
   afterStart = start + diff;

   // Add last segment
   if (prev !== start) result.push(start + ", " + afterStart + ".." + prev);
   else result.push(start);

   return "[" + result.join(", ") + "]";
}

/**
 * download  meta information about remote grib file 
 * update gribLimits object 
 * launch download binary grib datas
 * @param {string} directory
 * @param {string} current grib name
 */

async function gribMetaAndLoad(dir, model, gribName, load) {
   const gribParam = model ? `model=${model}` : `grib=${dir}/${gribName}`;
   const formData = `type=${REQ.GRIB}&${gribParam}`;
   console.log("Request sent:", formData);

   const headers = { "Content-Type": "application/x-www-form-urlencoded" };

   try {
      const response = await fetch(apiUrl, {
         method: "POST",
         headers,
         body: formData,
         cache: "no-store"
      });

      if (!response.ok) {
         throw new Error(`Error ${response.status}: ${response.statusText}`);
      }

      const data = await response.json();
      console.log("JSON received:", JSON.stringify(data));

      if (data._Error) {
         await Swal.fire({ icon: 'error', title: 'Error from server', text: `Error: ${data._Error}` });
         console.log("Error found:", data._Error);
         return null;
      }

      if (Object.keys(data).length === 0) {
         await Swal.fire({ icon: 'error', title: 'Error', text: 'Empty Grib or format error' });
         return null;
      }

      if (dir === "grib") {
         if (typeof gribLimits !== "undefined") {
            Object.assign(gribLimits, data);
            if (load) {
               try {
                  const result = await gribLoad(dir, model, gribName, data.nTimeStamp, data.nLat, data.nLon, data.nShortName);
                  dataGrib = result;
                  drawWind();
               } catch (err) {
                  console.error("Error gribLoad:", err);
                  await Swal.fire("Error gribload", err.message, "error");
               }
            }
            if (window.map) { // Important for call from r3files.html
               const bounds = [
                  [gribLimits.bottomLat, gribLimits.leftLon],
                  [gribLimits.topLat, gribLimits.rightLon]
               ];
               map.fitBounds(bounds);
            }
         }
      } else {
         gribLimits.currentName = gribName;
      }
      console.log ("end gribMetaAndLoad");
      return data; // To use metadata

   } catch (error) {
      console.error("Error meta:", error);
      await Swal.fire("Error meta", error.message, "error");
      return null;
   }
}

/**
 * display meta information about remote grib file 
 * update gribLimits object 
 * @param {string} directory
 * @param {string} current grib name
 */
async function gribInfo(dir, model, gribName) {
   const formatLat = x => (x < 0) ? -x + "°S" : x + "°N";
   const formatLon = x => (x < 0) ? -x + "°W" : x + "°E";

   Swal.fire({
      title: "Loading...",
      didOpen: () => Swal.showLoading(),
      allowOutsideClick: false,
      showConfirmButton: false
   });

   console.log ("before gribMetaAndLoad");
   const meta = await gribMetaAndLoad(dir, model, gribName, false);
   if (!meta || meta.centreName === undefined) return;
   const shortNames = Array.isArray(meta.shortNames) ? meta.shortNames.join(", ") : "NA";
   updateStatusBar();
   const centreName = (meta.centreName && meta.centreName.length > 0) ? `${meta.centreName}, ` : "";
   // console.log ("Meta", JSON.stringify (meta));

   let rows = [
      ["Centre", `${centreName}ID: ${meta.centreID}, Ed: ${meta.edNumber ?? "NA"}`],
      ["Time Run", `${meta.runStart.slice(11, 13)}Z`],
      ["From To UTC", `${meta.runStart} - ${meta.runEnd}`],
      ["File", `${meta.name} (${meta.fileSize.toLocaleString("fr-FR")} bytes, ${meta.fileTime})`],
      ["latStep lonStep", `${meta.latStep}° / ${meta.lonStep}°`],
      ["Zone", `${meta.nLat} x ${meta.nLon} values: lat: ${formatLat (meta.topLat)} to ${formatLat (meta.bottomLat)}, lon: ${formatLon (meta.leftLon)} to ${formatLon (meta.rightLon)}`],
      ["Short Names", shortNames],
      ["Time Stamps", `${meta.nTimeStamp} values: ${condenseTimeStamps(meta.timeStamps)}`]
   ];
  if (meta.info && meta.info.length >= 1) rows.push (["Info", `${meta.info}`]);

   const content = `
   <div style="padding: 16px; font-family: Arial, sans-serif;">
      <table style="border-collapse: collapse; width: 100%; text-align: left; font-size: 14px;">
         <tbody>
            ${rows.map(([key, value], index) => `
               <tr style="background-color: ${index % 2 === 0 ? '#f9f9f9' : '#ffffff'}; text-align: left;" >
                  <td style="padding: 8px 12px; font-weight: bold; border-bottom: 1px solid #ddd; text-align: left; min-width: 80px" >${key}</td>
                  <td style="padding: 8px 12px; border-bottom: 1px solid #ddd; text-align: left;" >${value}</td>
               </tr>
           `).join('')}
        </tbody>
      </table>
   </div>`;

   await Swal.fire({
      title: "Grib Info",
      width: '600px',
      html: content,
      icon: "success",
      confirmButtonText: "OK"
   });
}

async function selectModel() {
   let options = {};
   models.forEach(m => {
      options[m] = m; // key = value
   });

   // Affiche la modale
   const { value: model } = await Swal.fire({
      title: 'Model selection',
      input: 'select',
      inputOptions: options,
      inputPlaceholder: 'Choose Model',
      showCancelButton: true
   });
   if (!model) return;
   await gribInfo ("grib", model, "");
}

/**
 * choose a grib file then launch gribInfo 
 * @param {string} directory
 * @param {string} grib name
 */
async function chooseGrib (dir, gribName) {
   const fileName = await chooseFile(dir, "", false, 'Grib Select'); // wait selection
   if (!fileName) { console.log("No file"); return; }
   await gribInfo (dir, "", fileName);
}

/**
 * request Grib check from server
 */
function gribCheck () {
   if (!gribLimits || (!gribLimits.name && !gribLimits.currentName) || (gribLimits.name.length == 0 && gribLimits.currentName.length == 0)) {
      Swal.fire('Error Grib or Current', 'Wind or Current should be defined' , 'error');
      return;
   }
   const formData = `type=${REQ.GRIB_CHECK}&grib=grib/${gribLimits.name}&currentGrib=currentgrib/${gribLimits.currentName}`;
   console.log ("Request sent:", formData);
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   Swal.fire({
      title: 'Grib and Current check under way…',
      html: 'It may take time...',
      allowOutsideClick: false,
      didOpen: () => {
         Swal.showLoading();
      }
   });

   fetch(apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store"
   })
   .then(response => {
      if (!response.ok) {
         throw new Error(`Error ${response.status}: ${response.statusText}`);
      }
      return response.text();
   })
   .then(data => {
      if (data.includes("_Error")) {
         Swal.fire({ icon: 'error', title: 'Error from server', text: `${data}` });
         return;
      }
      else {
         let content = `<pre style="text-align: left; font-family: 'Courier New', Courier, monospace;`
         content += `font-size: 14px; background: #f4f4f4; padding: 10px; border-radius: 5px;">${data}</pre>`;

         Swal.fire({
            title: "Check Grib and Current Grib Report",
            html: content,
            width: "60%",
            footer: `grib: ${gribLimits.name}, currentGrib: ${gribLimits.currentName}`
         });
      }
   })
   .catch(error => {
      console.error("Error gribCheck:", error);
      Swal.fire({
         title: "Error",
         text: error.message,
         icon: "error",
         confirmButtonText: "OK"
      });
   });
}
