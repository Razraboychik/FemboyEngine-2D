# Resolved commit for every dependency FetchContent pulls in.
#
# The tags in FbyDependencies.cmake are the human-facing version, but a tag can
# be moved; these hashes are what the reproducible-build check compares against
# the actual checkout (`Source/Tools/Dev/CheckDependencyLock.py`). Update both
# the tag and this file in the same commit when a dependency is upgraded.

set(FBY_LOCK_SDL3_COMMIT "829a65d769d935c4852f8159e964312c0957260a")
set(FBY_LOCK_VULKAN_HEADERS_COMMIT "73814d006638aa00bd553215b054f0f02e457e22")
set(FBY_LOCK_GLSLANG_COMMIT "e1b562a8bed273a02f30b59b66a5d499793cede5")

# Vendored trees have no upstream git metadata in this checkout, so the pin is
# the SHA-256 of every sorted relative path plus its bytes. Any edited, added or
# removed file changes the digest.
# Box2D 3.2.0 (the version comes from its CMakeLists project()).
set(FBY_LOCK_BOX2D_TREE "366784a2457f7c04fa107c0cac4625c463943cf72ecf91a102c956c03728be98")
# Opus vendored snapshot; the download did not keep a tag or package_version.
set(FBY_LOCK_OPUS_TREE "5e80401f2297c644baa3428bdb7cd4327e9f53c15519d59dd0b84fc868fd68f4")
