// app.js

// Initialize the map
// Choose a default view (example: somewhere around Europe)
const map = L.map('map', {
    // You can set a simple CRS if you don't use tiles
    crs: L.CRS.EPSG3857
});

// Set an initial view (lat, lon, zoom)
map.setView([48.0, -4.0], 5); // Brittany as example

// Optional: set a plain background color via CSS (body or #map)
// Land polygons will be drawn on top.

// Load local GeoJSON for land polygons
fetch('geo/land_polygons.geojson')
    .then(response => {
        if (!response.ok) {
            throw new Error("Failed to load land_polygons.geojson");
        }
        return response.json();
    })
    .then(data => {
        const landLayer = L.geoJSON(data, {
            style: function () {
                return {
                    color: '#555555',     // border color
                    weight: 0.5,          // border width
                    fillColor: '#dddddd', // land fill color
                    fillOpacity: 1.0
                };
            }
        }).addTo(map);

        // Fit the map view to the land layer bounds
        try {
            map.fitBounds(landLayer.getBounds());
        } catch (e) {
            console.warn('Could not fit bounds:', e);
        }
    })
    .catch(err => {
        console.error('Error loading GeoJSON:', err);
    });

