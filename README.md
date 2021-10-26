# learn_rt
Beginner-level Ray Tracing repo.   

Step 1 - The rt_one_weekend is my implementation of Raytracing in One Weekend by Peter Shirley with my shabby C code:)  
I did it in C just for the heck of it. It made the implementation unnecessarily harder but more fun.   
For a better, cleaner and more efficient C take of the book refer to Eugene Morozov implementation   
https://github.com/Morozov-5F/raytracing-weekend  
I had to use OpenMP api for multithreading 
https://docs.microsoft.com/en-us/archive/msdn-magazine/2005/october/openmp-and-c-reap-the-benefits-of-multithreading-without-all-the-work
(Haven't finished win32 threads equivalent yet)   
Furthermore to speedup the troublesome Final Render scene (chapter 13) I decreased the number of random spheres:    
   
![_image_final_2](https://user-images.githubusercontent.com/74592722/125200521-d3151e80-e280-11eb-90e9-19df62b0fc11.jpg)
Here is the result of another experimental run:   
   
![_image_final_1](https://user-images.githubusercontent.com/74592722/125200548-e7591b80-e280-11eb-8f6f-ab95818b6a71.jpg)

Step 2 - Working on a beginner-level DXR samples... [WIP]   

2.1. DXR Hello Triangle
![dxr hello tri](https://github.com/omd24/learn_rt/blob/65f8732b7b51c2552a3450d463703d6be3332607/dxr_101_tutorials/dxr_hello_triangle/dxr_hello_tri.jpg)
