#
# Extracts all paths from a vsinc file. No duplicate paths are printed.
# when the sinc file is located in a different directory this path is 
# added to the resulting path


BEGIN{
  FS=",";
  OFS = " ";
  ORS = " ";
}
{    
    prefix = pathname(FILENAME) 
#    print prefix;
    path_add(pathname(pathcat(prefix,$3)),paths);
#    printf("%s - %s\n", prefix, 
    nrdeps = split($4,deps," ");
    for (i = 1; i <= nrdeps; i++){
        path_add(pathname(pathcat(prefix,deps[i])),paths);  
    }
}

END{
    # output all elements of the paths array
    for (x in paths){
      print x;
    }
}


function path_add(name,array){
    # add all non empty elements to the array
    # the index name is equal the the name that is added
    # this way all equal names only appear once in the array
    if(name != ""){
        array[name] = name;    
    }
}

function basename(filename,   pos){
    # split up the filename in directories and a filename
    pos = split(filename,parts,"/");
    # return the filename (which is the last element of the 
    # splitted path
    return parts[pos];
}

function pathname(filename,   result, pos, j){
    result = ""

    # split up the filename in directories and a filename
    pos = split(filename,parts,"/");
    # reconstruct the pathname by concatinating all elements
    # excluding the last element (which is the filename).
    if(pos > 1){
        result = parts[1];
        for(j = 2; j < pos; j++){
            result = sprintf("%s/%s",result,parts[j]);
        }
	}

    return result;
}

function pathcat(base,path){
	if(base != ""){
		return sprintf("%s/%s",prefix,path);
	} else {
		return path;
	}
}