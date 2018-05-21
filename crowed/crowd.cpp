#include <eosiolib/eosio.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/contract.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/currency.hpp>
#include <stdlib.h>
#include <time.h>
#include <eosiolib/crypto.h>
#include <utility>
#include <eosiolib/action.hpp>

using eosio::indexed_by;
using eosio::const_mem_fun;
using std::string;
using eosio::currency;
using eosio::asset;
using eosio::permission_level;
using eosio::action;

class crowd:public eosio::contract {
public:
	const uint64_t MULTIPLE_SCALE = 4000;
	const uint64_t DEPOSIT_SCALE = 3500;
	using contract::contract;
	crowd(account_name self):eosio::contract(self),
	deposits(_self,_self),
	crowds(_self,_self),
	withdraws(_self,_self){}
	/**
	*执行改操作成功后，需要后台向该用户发放RCC，因为该操作拿不到crowed的签名
	* @abi action
	*/
	void crow(account_name name, const asset & quantity) {
		require_auth(name);
		auto sy_n = S(4,"EOS");
		eosio_assert(quantity.symbol == sy_n,"只支持EOS的众筹");
		eosio_assert(quantity.amount > 0,"输入金额不能小于0");
		asset re_asset = quantity;

		auto my_sym = S(4,"RCC");
		asset my_asset = asset(re_asset.amount * MULTIPLE_SCALE,my_sym);

		//转入eos到crowed账户
		action(
			permission_level{name,N(active)},
			N(eosio.token),N(transfer),
			std::make_tuple(name,_self,quantity,std::string("crowed"))
			).send();

		auto name_crowds = crowd_index(_self,name);
		name_crowds.emplace(name,[&](auto &c){
			c.c_id = name_crowds.available_primary_key();
			c.name = name;
			c.c_asset = quantity;
			c.t_asset = my_asset;
		});
		crowds.emplace(_self,[&](auto &c){
			c.c_id = crowds.available_primary_key();
			c.name = name;
			c.c_asset = quantity;
			c.t_asset = my_asset;
		});

	}

	/**
	*执行操作成功后，需要后台向改用户发放RCC，因为该操作拿不到crowed的签名
	*@abi action
	*/
	void deposit(account_name name,asset quantity) {
		require_auth(name);
		auto sy_n = S(4,"EOS");
		eosio_assert(quantity.symbol == sy_n,"只支持EOS的充值");
		eosio_assert(quantity.amount > 0,"输入金额不能小于0");

		auto my_sym = S(4,"RCC");
		asset my_asset = asset(quantity.amount * DEPOSIT_SCALE,my_sym);

		//发送eos到crowed账户
		action(
			permission_level{name,N(active)},
			N(eosio.token),N(transfer),
			std::make_tuple(name,_self,quantity,std::string("deposit"))
			).send();

		//增加一条充值记录
		auto name_deposits = deposit_index(_self,name);
		name_deposits.emplace(name,[&](auto &d){
			d.d_id = name_deposits.available_primary_key();
			d.name = name;
			d.f_asset = quantity;
			d.d_asset = my_asset;
		});

		deposits.emplace(_self,[&](auto &d){

			d.d_id = deposits.available_primary_key();
			d.name = name;
			d.f_asset = quantity;
			d.d_asset = my_asset;
		});
	}

	/** 转账RCC到crowed账户，执行成功后后台需要转账EOS到用户的账户，
	* @abi action
	*/
	void withdraw(account_name name,asset quantity) {
		require_auth(name);
		auto sy_n = S(4,"RCC");
		eosio_assert(quantity.symbol == sy_n,"只支持RCC的众筹");
		eosio_assert(quantity.amount > 10000,"提现金额必须大于1万个");

		auto eos_sym = S(4,"EOS");
		asset eos_asset = asset(quantity.amount / DEPOSIT_SCALE,eos_sym);

		//name账户向crowed账户转账RCC
		action(
			permission_level{name,N(active)},
			N(eosio.token),N(transfer),
			std::make_tuple(name,_self,quantity,std::string("withdraw"))
			).send();

		//增加一条提现记录
		auto name_withdraws = withdraw_index(_self,name);
		name_withdraws.emplace(name,[&](auto &w){
			w.w_id = name_withdraws.available_primary_key();
			w.name = name;
			w.w_asset = quantity;
			w.t_asset = eos_asset;
		});
		
		withdraws.emplace(_self,[&](auto &w){
			w.w_id = withdraws.available_primary_key();
			w.name = name;
			w.w_asset = quantity;
			w.t_asset = eos_asset;
		});
	}


private:
	///@abi table crowdre i64
	struct crowdre {
		uint64_t c_id;
		account_name name;
		asset c_asset;
		asset t_asset;//获得的RCC
		time date = now();

		auto primary_key() const {return c_id;}
		account_name by_account_name() const {return name;}

		EOSLIB_SERIALIZE(crowdre,(c_id)(name)(c_asset)(t_asset)(date));
	};

	typedef eosio::multi_index<N(crowdre),crowdre,
	indexed_by<N(byname),
	const_mem_fun<crowdre,account_name,&crowdre::by_account_name>>> crowd_index ;
	
	///@abi table depositre i64
	struct depositre {

		uint64_t d_id;
		account_name name;
		asset f_asset;//消耗的eos
		asset d_asset;//获得的RCC
		time date = now();

		auto primary_key() const {return d_id;}
		account_name by_account_name() const {return name;}
		EOSLIB_SERIALIZE(depositre,(d_id)(name)(f_asset)(d_asset)(date));
	};

	typedef eosio::multi_index<N(depositre),depositre,
	indexed_by<N(byname),
	const_mem_fun<depositre,account_name,&depositre::by_account_name>>> deposit_index;

	/// @abi table withdrawre i64
	struct withdrawre {
		uint64_t w_id;
		account_name name;
		asset w_asset;//销毁的RCC
		asset t_asset;//获得eos;
		time date = now();

		auto primary_key() const {return w_id;}
		account_name by_account_name() const {return name;}
		EOSLIB_SERIALIZE(withdrawre,(w_id)(name)(w_asset)(t_asset)(date));
	};
	typedef eosio::multi_index<N(withdrawre),withdrawre,
	indexed_by<N(byname),
	const_mem_fun<withdrawre,account_name,&withdrawre::by_account_name>>> withdraw_index;

	crowd_index crowds;
	deposit_index deposits;
	withdraw_index withdraws;


};
EOSIO_ABI(crowd,(crow)(deposit)(withdraw))

