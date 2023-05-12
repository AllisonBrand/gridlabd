
#include <iostream>

#include "glengine.h"

int main(int argc, char *argv[])
{
	// prepare to catch exceptions from engine calls
	try {

		// connect to a gridlabd instance running the model in 'engine.glm'
		glengine engine;
		engine.set_debug_level(1);
		//engine.set_window(false); //this operation is currently not supported.
		//engine.connect(PACKAGE,"--pause","test_engine.glm",NULL);
		try{
			engine.connect("C:\\ciraci_gld\\VS2005\\Win32\\Debug","C:\\ciraci_gld\\VS2005\\Win32\\Debug\\test_engine.glm",NULL);
			//engine.connect(UDP,string("127.0.0.1"));
		}catch(const char *c){
			std::cout << "Launching gld failed:" << c << std::endl; 
			engine.shutdown();
			exit(0);
		}

		vector<string> exportNames=engine.getExportNames();
		vector<string> importNames=engine.getImportNames();

		int64_t time=0;
		// run until sync fails
		int count=10;
		do
		{	
			if(!engine.sync_start(time)){ //get the current time
			  std::cout << "TERM received from GLD" << std::endl;
			  engine.shutdown();
			}
			//get a value from gld
			std::cout << "Got time " << time << std::endl;
			string s;
			engine.get(importNames[0],s);
			std::cout << "Value of " << importNames[0] << " from gld is " << s << std::endl;
			count --;
			engine.sync_finalize(time+1); //sync one second ahead;
			//do something be quick about it
		}while(count > 0 );
		//since gld is not doing anything sync at TS_NEVER will cause gld to go to the end of the simulation
		engine.sync_start(time);
		engine.sync_finalize(TS_NEVER);
		if(engine.sync_start(time)!=0){
			std::cout << "Fail expecting term got something else " << std::endl;
			engine.shutdown();
		}
		engine.shutdown();
	
	} 
	catch (const char *msg)
	{
		cerr << "Example: " << msg << std::endl;
	}
}