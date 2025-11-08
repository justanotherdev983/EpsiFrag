glslc simple_shader.vert -o simple_shader.vert.spv
glslc simple_shader.frag -o simple_shader.frag.spv


glslc compute/first_half_kin.comp -o compute/first_half_kin.comp.spv 
glslc compute/full_potential.comp -o compute/full_potential.comp.spv 
glslc compute/last_half_kin.comp -o compute/last_half_kin.comp.spv 
glslc compute/visualize.comp -o compute/visualize.comp.spv 
