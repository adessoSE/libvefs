=================
 About releasing
=================

1. Tag the current vefs commit (the commit to be released) with the version
   specified in ``vcpkg.json`` using ``git tag -a <version>``. The version tag
   always starts with ``v``. Supply a message detailing all changes from a
   user perspective in bullet points (use markdown).
2. Push the tag with ``git push --tags``.
3. Open the next version for vefs by setting the ``version`` field in 
   ``vcpkg.json``.
4. Update the version in ``project(vefs VERSION 0.5.0.2 LANGUAGES CXX)`` in 
   ``CMakeLists.txt``.
5. Commit the new version with ``git commit -m '[MDK-XXXXX] Open version vX.Y.Z-U.W'``
6. Push the changes with ``git push``.
7. Potentially proceed with updating the vcpkg repository (see instructions
   there).
