// C++ program to demonstrate the use of 
// std::thread::hardware_concurrency() 

  
#include <chrono> 
#include <iostream> 
#include <thread> 

using namespace std; 

  

int main() 
{ 

    unsigned int con_threads; 

  

    // calculating number of concurrent threads 

    // supported in the hardware implementation 

    con_threads = thread::hardware_concurrency(); 

  

    cout << "Number of concurrent threads supported are: "

         << con_threads << endl; 

  

    return 0; 
} 
