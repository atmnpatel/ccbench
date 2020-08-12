#include "river.hh"
#include "interface.h"
#include "masstree_beta_wrapper.h"

using namespace ccbench;

bool init_table_warehouse(size_t nwh){
  Token token{};
	init();
  enter(token);
  //insert(token, Storage::WAREHOUSE, a, b);
	
	std::time_t now = std::time(nullptr);
	{

        std::string mem;
        
		//CREATE Warehouses by single thread.
		for (size_t w = 0; w < nwh; w++) {
			TPCC::Warehouse wh;
			wh.W_ID = w;
			wh.W_TAX = 1.5;
			wh.W_YTD = 1000'000'000;

            char* ptr=reinterpret_cast<char*>(&wh);
            size_t siz=sizeof(TPCC::Warehouse);
            //std::cout<<siz<<std::endl;
            insert(token, Storage::WAREHOUSE, wh.createKey(), {ptr,siz});
            mem=wh.createKey();
        }

        commit(token);

        
        Tuple *ret;
        if(search_key(token,Storage::WAREHOUSE,mem,&ret)==Status::WARN_NOT_FOUND){
            std::cout<<"not found"<<std::endl;
        }else{
            std::cout<<"ok"<<std::endl;
        }
        commit(token);
        
        leave(token);
        fin();
        
       
	}
	commit(token);
	leave(token);
	fin();
}

void
makedb_tpcc()
{
  Token token{};
  std::string a{"a"};
  std::string b{"b"};
	
	init();
  enter(token);
  insert(token, Storage::CUSTOMER, a, b);
  Tuple *ret_tuple_ptr;
  commit(token);
	
  delete_record(token, Storage::CUSTOMER, a);
	commit(token);
  search_key(token, Storage::CUSTOMER, a, &ret_tuple_ptr);
  commit(token);
  leave(token);
	fin();
}

int
//tpcc_txn_man::run_new_order(tpcc_query * query)
main(void)
{
<<<<<<< HEAD
    //	makeDBforTPCC();
    load(1);
=======
	makedb_tpcc();
	const int nwh = 224;
	init_table_warehouse(nwh);
	
>>>>>>> 67493a9e3ffbb3bed77070f416cedaaa8a508219
	return 0;
}
