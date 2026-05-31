file(REMOVE_RECURSE
  "libcurl.a"
  "libcurl.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/libcurl_static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
