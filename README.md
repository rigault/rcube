# RCube

**RCube** is a routing software for sailing. It computes the optimal route from a starting point (`pOr`) to a destination point (`pDest`) at a given `START_TIME`, using GRIB weather data and a boat polar diagram.

## Isochrone Method

RCube uses the **isochrone method**:
- From the origin point `pOr`, it calculates all points reachable within a time interval `T_STEP`. This is the initial isochrone.
- From each point of the previous isochrone, it computes the next reachable points.
- This process repeats until the destination `pDest` is reached, or until the available weather data is exhausted.

The resulting route is either:
- The optimal path from `pOr` to `pDest`, or
- A partial route ending at the closest point to `pDest` from the last computed isochrone.

## GRIB Files

**GRIB** (GRIdded Binary) files provide meteorological data over geographical coordinates (latitude and longitude). RCube uses:
- Wind at 10 meters altitude
- Wave height and direction
- Optionally: ocean currents

## Polar Diagram

The **polar diagram** describes the boat’s speed based on:
- **TWS** (True Wind Speed)
- **TWA** (True Wind Angle), the angle between the wind and the boat’s axis

Wave polars can also be used. They apply a speed multiplier depending on wave height and the angle of waves relative to the boat. For example:
- Head seas may reduce speed (e.g., 60%)
- Following seas may increase speed (e.g., 120%)

## Routing Parameters

- `DAY_EFFICIENCY`, `NIGHT_EFFICIENCY`: Factors that scale boat speed to account for crew performance (e.g., 80% efficiency).
- `THRESHOLD`: Minimum sailing speed. Below this, the engine is used.
- `MOTOR_S`: Boat speed when using the engine.
- `X_WIND`: Wind multiplier (e.g., 1.2 to simulate stronger wind).
- `MAX_WIND`: Maximum wind speed tolerated. Routes will avoid stronger wind.
- `PENALTY0`: Minutes lost during a tack.
- `PENALTY1`: Minutes lost during a gybe.
- `PENALTY2`: Minutes lost during a sail change.
- `RANGE_COG`: Course variation range (e.g., ±90° from direct heading).
- `COG_STEP`: Course resolution in degrees (e.g., 5° steps).

## Abbreviations

- **Lat**: Latitude  
- **Lon**: Longitude  
- **pOr**: Point of origin  
- **pDest**: Destination point  

## Acronyms

- **TWS**: True Wind Speed  
- **TWD**: True Wind Direction  
- **TWA**: True Wind Angle  
- **SOG**: Speed Over Ground  
- **COG**: Course Over Ground  
- **AWA**: Apparent Wind Angle  
- **AWS**: Apparent Wind Speed  
- **HDG**: Heading (boat’s axis angle to true north)  
- **VMG**: Velocity Made Good  
- **VMC**: Velocity Made on Course  

When sailing upwind:  
**VMG = cos(TWA) × TWS**

## Units

- Distances: nautical miles (NM)  
- Speeds: knots (kn)  
- GRIB wind data: meters per second (m/s)

---

## 🖥 Architecture

RCube is divided into two main components:

### 🔧 Backend – High-performance C

The backend is written in **C** for performance reasons, especially for numerical routing algorithms. It exposes a **RESTful JSON API** that can be queried with routing parameters and returns route data.

This separation enables:
- Efficient computations
- Reusability in scripts or external clients
- Easy integration with web frontends

### 🌐 Frontend – JavaScript

The frontend is implemented using **JavaScript**, along with **HTML** and **CSS**. It provides:
- An interface to input routing settings
- Visual rendering of the computed route
- Weather and performance visualization (via APIs like Windy or Google Maps)

