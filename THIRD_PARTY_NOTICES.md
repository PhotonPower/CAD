# Third-party software

Nurbforge CAD (MIT, see LICENSE) uses:

| Component | Licence | Use |
|---|---|---|
| [Qt 6](https://www.qt.io) | LGPL-3.0 (dynamically linked) | GUI, rendering (RHI), concurrency. The Qt libraries shipped with the Windows installer and the macOS bundle can be replaced by compatible versions; Linux packages use the distribution's Qt. Source: https://download.qt.io |
| [Nurbforge](https://github.com/PhotonPower/Nurbforge) | MIT | NURBS kernel (statically linked) |
| [CDT](https://github.com/artem-ogre/CDT) 1.4.4 | MPL-2.0 (unmodified, header-only) | Constrained Delaunay triangulation of trimmed faces. Source: https://github.com/artem-ogre/CDT |
