/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2014 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
#include <iostream>
#include <fstream>
#include "pin.H"
#include <vector>
#include <iomanip>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include <map>

#include <stdlib.h>

char application_name[1000];

typedef struct indirect_inst_item{
	UINT32 inst_image_id;
	UINT32 target_image_id;
	ADDRINT inst_addr;
	ADDRINT target_addr;
}INDIRECT_INST_ITEM;

stringstream ss;
map<string, INDIRECT_INST_ITEM> indirect_inst_map;

typedef struct image_item{
	string image_name;
	IMG_TYPE type;
	UINT32 image_id;
}IMAG_ITEM;
vector<IMAG_ITEM> image_vec;

BOOL image_is_match(string name, UINT32 id)
{
	for(vector<IMAG_ITEM>::iterator iter = image_vec.begin(); iter!=image_vec.end(); iter++){
		if((*iter).image_name == name){
			if((*iter).image_id == id)
				return true;
			else
				return false;
		}
	}
	return false;
}

bool exist_old_log = false;
vector<IMAG_ITEM> old_image_vec;

BOOL is_match_old_log(IMAG_ITEM new_item)
{
	char new_image_name[1024] = "\0";
	if(new_item.type == IMG_TYPE_SHAREDLIB){
		UINT32 ret = readlink(new_item.image_name.c_str(), new_image_name, 1024); 
		if(ret<=0)
			cerr<<"readlink error!"<<endl;
		if(new_image_name[0]=='\0')
			sprintf(new_image_name, "%s", new_item.image_name.c_str());
	}else
		sprintf(new_image_name, "%s", new_item.image_name.c_str());

	for(vector<IMAG_ITEM>::iterator it = old_image_vec.begin(); it!=old_image_vec.end(); it++){
		if(strcmp(new_image_name, (*it).image_name.c_str())==0 && new_item.type==(*it).type && new_item.image_id==(*it).image_id)
			return true;
	}
	return false;
}

VOID ImageLoad(IMG img, VOID *v)
{
	//cout << "Loading " << IMG_Name(img) << ", Image addr=0x" <<hex<< IMG_LowAddress(img) <<"-0x"<<hex<< IMG_HighAddress (img)<<endl;
 	IMAG_ITEM item = {IMG_Name(img), IMG_Type(img), IMG_Id(img)};
	if(!exist_old_log || is_match_old_log(item))
		image_vec.push_back(item);
	else{
		cerr<<"Do not match old log!"<<endl;
		abort();
	}
		
}

VOID record_target(ADDRINT inst_address, ADDRINT inst_target)
{
	PIN_LockClient();
	IMG inst_image = IMG_FindByAddress(inst_address);
	UINT32 inst_image_id = IMG_Id(inst_image);
	IMG inst_target_image = IMG_FindByAddress(inst_target);
	UINT32 inst_target_image_id = IMG_Id(inst_target_image);
	if(IMG_Valid(inst_image) && IMG_Valid(inst_target_image)){
		if(image_is_match(IMG_Name(inst_image), inst_image_id) && image_is_match(IMG_Name(inst_target_image), inst_target_image_id)){
			INDIRECT_INST_ITEM item = {inst_image_id, inst_target_image_id, inst_address-IMG_LowAddress(inst_image), inst_target-IMG_LowAddress(inst_target_image)};
			ss<<item.inst_image_id<<"."<<item.target_image_id<<"."<<item.inst_addr<<"."<<item.target_addr;
			string key = ss.str();
			ss.str("");
			indirect_inst_map.insert(make_pair(key, item));
		}else{
			cerr<<"image is not match"<<endl;
			abort();
		}
	}
	PIN_UnlockClient();
}
// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
	// Insert a call to docount before every instruction, no arguments are passed
	if(INS_IsIndirectBranchOrCall(ins) && !INS_IsRet(ins)){
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)record_target, IARG_INST_PTR, IARG_BRANCH_TARGET_ADDR, IARG_END);
	}
}

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "indirect.out", "specify output file name");

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
	if(exist_old_log && old_image_vec.size()!=image_vec.size()){
		cerr<<"Oh no, old log image num is not match!"<<endl;
		return ;
	}
	
	ofstream OutFile;
	OutFile.open(application_name);
	// Write to a file since cout and cerr maybe closed by the application
	OutFile.setf(ios::hex, ios::basefield);
	//OutFile.setf(ios::showbase);
	OutFile<<"IMG_NUM= "<<image_vec.size()<<endl;
	for(vector<IMAG_ITEM>::iterator iter = image_vec.begin(); iter!=image_vec.end(); iter++){
		if((*iter).type == IMG_TYPE_SHAREDLIB){
			char process_real_path[1024] = "\0";
			UINT32 ret = readlink((*iter).image_name.c_str(), process_real_path, 1024); 
			if(ret<=0)
				cerr<<"readlink error!"<<endl;
			
			if(process_real_path[0]!='\0')
				OutFile<<setw(2)<<(*iter).image_id<<" "<<process_real_path<<" "<<endl;
			else
				OutFile<<setw(2)<<(*iter).image_id<<" "<<(*iter).image_name.c_str()<<" "<<endl;
		}else
			OutFile<<setw(2)<<(*iter).image_id<<" "<<(*iter).image_name<<" "<<endl;
	}
	OutFile<<"INST_NUM= "<<indirect_inst_map.size()<<endl;
	for(map<string, INDIRECT_INST_ITEM>::iterator iter = indirect_inst_map.begin(); iter!=indirect_inst_map.end(); iter++){
		INDIRECT_INST_ITEM item = iter->second;
		OutFile<<item.inst_addr<<" ( "<<item.inst_image_id<<" )--> "<<item.target_addr<<" ( "<<item.target_image_id<<" )"<<endl;
	}
	OutFile<<"END"<<endl;
	OutFile.close();
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool counts the number of dynamic instructions executed" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */
/*   argc, argv are the entire command line: pin -t <toolname> -- ...    */
/* ===================================================================== */

void read_old_log()
{
	ifstream ifs;
	ifs.open(application_name, ifstream::in);
	if(ifs.is_open()){
		old_image_vec.clear();
		//read head log
		int image_num = 0;
		string str_image_num;
		UINT32 id;
		ifs>>str_image_num>>image_num;
		for(int idx=0; idx<image_num; idx++){
			string image_path, name;
			ifs>>id>>image_path;
			IMG_TYPE type = idx==0 ? IMG_TYPE_SHARED : IMG_TYPE_SHAREDLIB;
			IMAG_ITEM item = {image_path, type, id};
			old_image_vec.push_back(item);
		}
		//read old profile inst information
		string padding;
		char c;
		int inst_num = 0;
		ifs>>hex>>padding>>inst_num;
		for(int idx=0; idx<inst_num; idx++){
			ADDRINT inst, target;
			UINT32 inst_idx, target_idx;
			ifs>>hex>>inst>>c>>inst_idx>>padding>>target>>c>>target_idx>>c;
			INDIRECT_INST_ITEM item = {inst_idx, target_idx, inst, target};
			ss<<item.inst_image_id<<"."<<item.target_image_id<<"."<<item.inst_addr<<"."<<item.target_addr;
			string key = ss.str();
			ss.str("");
			indirect_inst_map.insert(make_pair(key, item));
		}
		ifs.close();
		exist_old_log = true;
	}else
		exist_old_log = false;
}

int main(int argc, char * argv[])
{
	// Initialize pin
	if (PIN_Init(argc, argv)) return Usage();

	char *path_end = strrchr(argv[12],  '/');
	if(!path_end)
		path_end = argv[12];
	else
		path_end++;
	
	sprintf(application_name, "/home/wangzhe/spec_profile/%s.indirect.log", path_end);
	
	read_old_log();
	// Register Instruction to be called to instrument instructions
	INS_AddInstrumentFunction(Instruction, 0);

	// Register Fini to be called when the application exits
	PIN_AddFiniFunction(Fini, 0);

	// find all image and it's range
	IMG_AddInstrumentFunction(ImageLoad, 0);
	// Start the program, never returns
	PIN_StartProgram();

	return 0;
}
