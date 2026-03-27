# TODO

- [ ] Merge SConstruct back into one file and clean things up related to string/path handling in patch_dol etc.
- [ ] handle calling C++ names in the map - maybe the dol_c_kit already does this with the name mangling?
- [ ] complete the map file - have all the data 
- [ ] make it so we dont have to write isos every time
    => https://youtube.com/watch?v=plUi3Ak-B98
    - it seems like dolphin will behave nicely with any extracted iso when you just launch the main.dol
- [ ] convenient vscode task for relaunching dolphin

- [ ] note somewhere in instructions you need zlib for pillow build
    (msys/zlib-devel)
    actually it's more than zlib. (https://pillow.readthedocs.io/en/latest/installation/building-from-source.html#building-from-source)
    see notes.md 3/26 