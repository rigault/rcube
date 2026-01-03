/**
 * Port markers (Leaflet) with visibility rules based on zoom level and port importance.
 *
 * Rules:
 * - level = 1 (major ports): visible from ZOOM_LEVEL_1
 * - level = 0 (minor ports): visible from ZOOM_LEVEL_0 (higher zoom => more detail)
 *
 * Notes:
 * - No automatic name display (no tooltips). Only popups are kept.
 * - Two different marker colors:
 *   - level=1: black
 *   - level=0: grey
 *
 * Prerequisites:
 * - Leaflet must be loaded (L available)
 * - `ports` must be defined as an array like:
 *   [{ lat: Number, lon: Number, name: String, level: 0|1 }, ...]
 */

/** Zoom threshold to show major ports (level=1). */
const ZOOM_LEVEL_1 = 7;

/** Zoom threshold to also show minor ports (level=0). */
const ZOOM_LEVEL_0 = 11;

/**
 * Leaflet layer for major ports (level=1).
 * @type {L.LayerGroup}
 */
const portsLayerLevel1 = L.layerGroup();

/**
 * Leaflet layer for minor ports (level=0).
 * @type {L.LayerGroup}
 */
const portsLayerLevel0 = L.layerGroup();

/**
 * Creates a Leaflet divIcon containing a marine-style SVG marker.
 *
 * @param {string} fillColor - Fill color for the circular marker background.
 * @returns {L.DivIcon} The configured Leaflet icon.
 */
function makePortIcon(fillColor) {
  return L.divIcon({
    className: "port-icon",
    html: `
      <svg width="24" height="24" viewBox="0 0 24 24" aria-hidden="true">
        <!-- background disc -->
        <circle cx="12" cy="12" r="9" fill="${fillColor}" />
        <!-- white ring -->
        <circle cx="12" cy="12" r="9" fill="none" stroke="#fff" stroke-width="1.6" />

        <!-- anchor -->
        <g transform="translate(12 12)" fill="none" stroke="#fff" stroke-width="1.7"
           stroke-linecap="round" stroke-linejoin="round">
          <circle cx="0" cy="-6.2" r="1.3"/>
          <path d="M0 -4.8 V5.2"/>
          <path d="M-4.2 -3.2 H4.2"/>
          <path d="M0 5.2 C-1.8 5.2 -3.7 4.3 -4.6 2.9"/>
          <path d="M0 5.2 C1.8 5.2 3.7 4.3 4.6 2.9"/>
          <circle cx="0" cy="5.5" r="0.6" fill="#fff" stroke="none"/>
        </g>
      </svg>
    `,
    iconSize: [24, 24],
    iconAnchor: [12, 12] // exact center
  });
}

/** Icon for major ports (level=1): black. */
const portIconLevel1 = makePortIcon("#000");

/** Icon for minor ports (level=0): grey. */
const portIconLevel0 = makePortIcon("#888");

/**
 * Creates all port markers and registers them in their respective global layer groups:
 * - level=1 markers go to `portsLayerLevel1`
 * - level=0 markers go to `portsLayerLevel0`
 *
 * Only popups are bound (no automatic name display).
 *
 * @param {Array<{lat:number, lon:number, name:string, level:number}>} ports - Ports list.
 */
function buildPortMarkers(ports) {
  ports.forEach(p => {
    const safeName = (p.name ?? "").toString();
    const displayName = safeName.replaceAll("_", " ");

    const icon = (p.level === 1) ? portIconLevel1 : portIconLevel0;

    const marker = L.marker([p.lat, p.lon], {
      icon,
      title: displayName
    });

    // Keep only popup (no tooltip / no auto label)
    marker.bindPopup(`<b>${displayName}</b>`);

    if (p.level === 1) portsLayerLevel1.addLayer(marker);
    else portsLayerLevel0.addLayer(marker);
  });
}

/**
 * Updates visibility of port layers based on current map zoom.
 *
 * Behaviour:
 * - Below ZOOM_LEVEL_1: no ports displayed
 * - From ZOOM_LEVEL_1 to below ZOOM_LEVEL_0: only level=1 ports displayed
 * - From ZOOM_LEVEL_0 and above: all ports displayed
 *
 * @param {L.Map} map - The Leaflet map instance.
 */
function updatePortsVisibility(map) {
  const z = map.getZoom();

  const showLevel1 = z >= ZOOM_LEVEL_1;
  const showLevel0 = z >= ZOOM_LEVEL_0;

  // Major ports layer
  if (showLevel1) {
    if (!map.hasLayer(portsLayerLevel1)) portsLayerLevel1.addTo(map);
  } else {
    if (map.hasLayer(portsLayerLevel1)) map.removeLayer(portsLayerLevel1);
  }

  // Minor ports layer
  if (showLevel0) {
    if (!map.hasLayer(portsLayerLevel0)) portsLayerLevel0.addTo(map);
  } else {
    if (map.hasLayer(portsLayerLevel0)) map.removeLayer(portsLayerLevel0);
  }
}

/**
 * Installs the ports system:
 * - builds markers from global `ports`
 * - wires zoom handler
 * - applies initial visibility
 *
 * @param {L.Map} map - The Leaflet map instance.
 * @param {Array<{lat:number, lon:number, name:string, level:number}>} ports - Ports list.
 */
function initPorts(map, ports) {
  buildPortMarkers(ports);

  // Update on zoom changes
  map.on("zoomend", () => updatePortsVisibility(map));

  // Initial state
  updatePortsVisibility(map);
}

/**
 * Displays a searchable and filterable list of ports using SweetAlert2.
 *
 * Features:
 * - Filter by port level (0, 1, or all)
 * - Displays: name, level, formatted coordinates
 *
 * Requirements:
 * - SweetAlert2 (Swal) must be loaded
 * - latLonToStr(lat, lon, DMS_DISPLAY.DMS) must exist
 *
 * @param {Array<{name:string, lat:number, lon:number, level:number}>} ports
 */
function dumpPorts(ports = ports) {
  const nTotal  = ports.length;
  const nMinor  = ports.reduce((n, p) => n + (p.level === 0), 0);
  const nMajor  = nTotal - nMinor;
  

  /** Builds the HTML table for the given list of ports */
  function buildTable(data) {
    const rows = data.map(p => {
      const coords = latLonToStr(p.lat, p.lon, DMS_DISPLAY.DMS);
      const name = p.name.replaceAll("_", " ");

      return `
        <tr>
          <td style="text-align:left">${name}</td>
          <td style="text-align:center">${p.level}</td>
          <td style="text-align:center; font-family:monospace">${coords}</td>
        </tr>
      `;
    }).join("");

    return `
      <table class="swal2-table" style="width:100%; border-collapse:collapse;">
        <thead>
          <tr style="background-color: orange; color:white">
            <th style="text-align:left">Name</th>
            <th style="text-align:center">Level</th>
            <th style="text-align:center">Coords</th>
          </tr>
        </thead>
        <tbody>
          ${rows}
        </tbody>
      </table>
    `;
  }

  /** Applies the selected level filter and updates the table */
  function updateTable() {
    const level = document.getElementById("portLevelFilter").value;

    let filtered = ports;
    if (level !== "all") {
      filtered = ports.filter(p => String(p.level) === level);
    }
    document.getElementById("portsTable").innerHTML = buildTable(filtered);
  }

  // SweetAlert2 modal
  Swal.fire({
    title: "Ports list",
    width: "60%",
    html: `
      <div style="text-align:left; margin-bottom:8px;">
        <label>
          Show ports with level:
          <select id="portLevelFilter" style="margin-left:8px;">
            <option value="all">All</option>
            <option value="1">Level 1 (major)</option>
            <option value="0">Level 0 (minor)</option>
          </select>
        </label>
      </div>

      <div id="portsTable" style="max-height:60vh; overflow:auto;"></div>
    `,
    footer: `Total ports: ${nTotal}, Major: ${nMajor}, Minor: ${nMinor}`,
    didOpen: () => {
      document
        .getElementById("portLevelFilter")
        .addEventListener("change", updateTable);

      updateTable(); // initial render
    }
  });
}
