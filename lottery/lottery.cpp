#include <eosiolib/eosio.hpp>
#include <eosiolib/multi_index.hpp>
#include <eosiolib/contract.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/currency.hpp>
#include <string>
#include <vector>
#include <time.h>
#include <eosiolib/crypto.h>
#include <utility>
#include <iostream>

using eosio::indexed_by;
using eosio::const_mem_fun;
using std::string;
using eosio::currency;
using eosio::asset;
using eosio::permission_level;
using eosio::action;
class lottery:public eosio::contract {

public:
	using contract::contract;
	lottery(account_name self):eosio::contract(self),
	games(_self,_self),
	players(_self,_self),
	onetoonegames(_self,_self),
	one2oneres(_self,_self),
	dicegames(_self,_self),
	diceres(_self,_self){}
	
	/** 玩家加入游戏
	* id 表示加入那句游戏
	* @abi action
	*/
	void join(account_name name,uint64_t number,uint64_t id) {
		require_auth(name);
		auto itr = games.find(id);
		eosio_assert(itr->current_index < 100 && itr->current_index >= 0,"已经达到人数最大限度");
		eosio_assert(itr != games.end(),"该局游戏不存在");
		eosio_assert(itr->current_index < 100,"已经达到人数最大限度");
		auto player_game_index = players.get_index<N(bygid)>();
		auto game_itr = player_game_index.find(id);

		while(game_itr != player_game_index.end() && game_itr->g_id == id) {
			eosio_assert(game_itr->player_name != name,"已经加入游戏");
			eosio::print("遍历出的玩家：",eosio::name{game_itr->player_name} ,"玩家 id：",game_itr->r_id,"\n");
			++game_itr;
		}
		games.modify(itr,_self,[&](auto &g){
			g.current_index = g.current_index + 1;
		});

		time date = now();
		auto record_index = palyer_table_type(_self,name);
		record_index.emplace(_self,[&](auto &r){
			r.g_id = id;
			r.number = number;
			r.player_name = name;
			r.r_id = record_index.available_primary_key();
			r.date = date;
		});

		players.emplace(_self,[&](auto &r){
			r.g_id = id;
			r.number = number;
			r.player_name = name;
			r.r_id = players.available_primary_key();
			r.date = date;
		});
	}

	/** 开始游戏,游戏id
	** @abi action
	*/
	void start(uint64_t g_id) {
		auto itr = games.find(g_id);
		eosio_assert(itr != games.end(),"游戏不存在");
		eosio_assert(itr->current_index == 100,"游戏人数不对，无法开始");
		game_rule(g_id);
	}

	/** 开一局游戏,指定本局游戏筹码数量
	* @abi action
	*/

	void open(uint64_t pay) {
		games.emplace(_self,[&](auto &g){
			g.g_id = games.available_primary_key();
			g.current_index = 0;
			g.pay = pay;
		});
	}


	/**
	摇色子游戏庄家需要拥有的筹码
	*@abi action 
	*开一局摇色子游戏
	*/
	void opendicegame(account_name banker,uint8_t player_num) {
		require_auth(_self);//必须是我们自己开庄
		eosio_assert(player_num > 0,"最少设置一个玩家参加");
		dicegames.emplace(_self,[&](auto &g){
			g.g_id = dicegames.available_primary_key();
			g.player_num = player_num;
			g.banker = _self;
		});
	}

	/**
	*@abi action 
	*玩家加入摇色子游戏
	*/

	void joindicegame(uint64_t g_id,account_name name,asset quantity,uint8_t detain_type) {
		require_auth(name);//需要用户授权才能扣除用户资金
		eosio_assert(detain_type == DiceDetainType::big|| detain_type == DiceDetainType::small 
			|| detain_type == DiceDetainType::leopard,"押注类型错误");
		eosio_assert(quantity.is_valid(),"invalid quantity");
		eosio_assert(quantity.amount > 0,"amount must positive quantity");
		check_my_asset(quantity);

		auto itr = dicegames.find(g_id);
		eosio_assert(itr != dicegames.end(),"该局游戏不存在");
		eosio_assert(itr->current_index != itr->player_num ,"人数已满");
		
		auto dice_game_index = diceres.get_index<N(bygid)>();
		auto game_itr = dice_game_index.find(g_id);
		while(game_itr != dice_game_index.end() && game_itr->g_id == g_id) {
			eosio_assert(game_itr->player_name != name,"已经加入游戏");
			++game_itr;
		}

		time date = now();
		//具体玩家为scope建表
		auto record_index = dice_record_type(_self,name);
		record_index.emplace(name,[&](auto &r){
			r.r_id = record_index.available_primary_key();
			r.g_id = g_id;
			r.detain_type = detain_type;
			r.bet = quantity;
			r.player_name = name;
			r.date = date;
		});

		//当前合约为scope建表
		diceres.emplace(_self,[&](auto &r){
			r.r_id = diceres.available_primary_key();
			r.g_id = g_id;
			r.detain_type = detain_type;
			r.bet = quantity;
			r.player_name = name;
			r.date = date;
		});
		dicegames.modify(itr,_self,[&](auto &g){

			g.current_index = itr->current_index + 1;
		});
		// action(permission_level{name,N(active)},
		// 	N(eosio.token),N(transfer),
		// 	std::make_tuple(name,_self,quantity,string(""))
		// 	).send();

	}

	/**

	* @abi action 
	*开奖摇色子游戏
	*/
	void startdicegame(uint64_t g_id, uint8_t one, uint8_t two,uint8_t three ,uint64_t randseed) {
		require_auth(_self);
		eosio_assert(one>=1 && one<= 6,"传入开奖号码有误");
		eosio_assert(two>=1 && two<= 6,"传入开奖号码有误");
		eosio_assert(three>=1 && three<= 6,"传入开奖号码有误");

		auto itr = dicegames.find(g_id);
		eosio_assert(itr != dicegames.end(),"该局游戏不存在");
		eosio_assert(itr->end != true,"已经开奖");
		eosio_assert(itr->player_num == itr->current_index,"未达到要求人数");

		DiceDetainType type;
		uint8_t result = one + two + three;
		if(result >= 11 ){
			type = DiceDetainType::big;
		}
		if(result <= 10) {
			type = DiceDetainType::small;
		}
		if((one==two) && (two == three)) {
			type = DiceDetainType::leopard;
		}

		auto dice_game_index = diceres.get_index<N(bygid)>();
		auto game_itr = dice_game_index.find(g_id);
		while(game_itr != dice_game_index.end() && game_itr->g_id == g_id) {
			if(game_itr->detain_type == type) {
				///豹子发放三倍奖励
				if(type == DiceDetainType::leopard) {
					action(permission_level{_self,N(active)},
					N(eosio.token),N(transfer),
					std::make_tuple(_self,game_itr->player_name,game_itr->bet * 24,std::string(""))
					).send();
				} else {
					action(permission_level{_self,N(active)},
						N(eosio.token),N(transfer),
						std::make_tuple(_self,game_itr->player_name,game_itr->bet * 2,std::string(""))
						).send();
				}
			}
			++game_itr;
		}
		dicegames.modify(itr,_self,[&](auto &g){
					g.randseed = randseed;
					g.end = true;
					g.one = one;
					g.two = two;
					g.three = three;
				});

	}

	/**
	* 游戏一直未满，管理员主动结束，返回资金给竞猜者
	* @abi action 
	*/
	void stopdicegame(uint64_t g_id) {
		require_auth(_self);
		auto itr = dicegames.find(g_id);
		eosio_assert(itr != dicegames.end(),"该局游戏不存在");
		eosio_assert(itr->end != true,"已经开奖");

		auto dice_game_index = diceres.get_index<N(bygid)>();
		auto game_itr = dice_game_index.find(g_id);
		while(game_itr != dice_game_index.end() && game_itr->g_id == g_id) {
			//返还用户金额
			action(permission_level{_self,N(active)},
				N(eosio.token),N(transfer),
				std::make_tuple(_self,game_itr->player_name,game_itr->bet,string(""))
				).send();
			++game_itr;
		}
		dicegames.modify(itr,_self,[&](auto &g){
				g.randseed = 0;
				g.end = true;
			});
	}

	/// @abi action 
	///加入双人游戏
	void joinpair(uint64_t g_id,account_name name, asset  quantity,uint8_t number) {
		require_auth(name);
		eosio_assert(number == 1 || number == 0,"竞猜数字只能是1或者0");
		eosio_assert(quantity.is_valid(),"invalid quantity");
		eosio_assert(quantity.amount > 0,"amount must positive quantity");
		check_my_asset(quantity);

		auto itr = onetoonegames.find(g_id);
		eosio_assert(itr != onetoonegames.end(),"该局游戏不存在");
		eosio_assert(itr->player_num <= 1,"已经达到人数最大限制");
	
		auto one2one_game_index = one2oneres.get_index<N(bygid)>();
		auto game_itr = one2one_game_index.find(g_id);
		while(game_itr != one2one_game_index.end() && game_itr->g_id == g_id) {
			//eosio_assert(0,"测试是否为循环出错");
			eosio_assert(game_itr->player_name != name,"已经加入游戏");
			eosio_assert(game_itr->number == number,"不能竞猜同一结果");
			++game_itr;
		}
		if(itr->player_num == 0) {
			onetoonegames.modify(itr,_self,[&](auto & g){
				g.player_num = 1;
				g.bet = quantity;
			});
		} else {
			//eosio_assert(0,"测试是否是这里出错");
			eosio_assert(quantity == itr->bet,"玩家之间筹码需要一致");
			onetoonegames.modify(itr,_self,[&](auto & g){
				g.player_num = 2;
			});
		}

		time date = now();
		auto record_index = pair_record_type(_self,name);
		record_index.emplace(name,[&](auto &r){
			r.r_id = record_index.available_primary_key();
			r.number = number;
			r.g_id = g_id;
			r.player_name = name;
			r.bet = quantity;
			r.date = date;
		});

		one2oneres.emplace(_self,[&](auto &r){
			r.r_id = one2oneres.available_primary_key();
			r.number = number;
			r.g_id = g_id;
			r.player_name = name;
			r.bet = quantity;
			r.date = date;
		});
		
		//转入金额到lotter账户
	 	action(
	 		permission_level{name,N(active)},
	 		N(eosio.token),N(transfer),
	 		std::make_tuple(name,_self,quantity,std::string("bet"))
	 		).send();
	 	 
	 }

	/**
	*@abi action 
	*开一局两人对战游戏，
	*/
	void startpairgame(uint64_t g_id,uint8_t result,uint64_t randseed) {
		require_auth(_self);//需要管理者签名,因为需要管理者向赢钱的加入游戏
		eosio_assert(result == 0 || result == 1,"请传入正确的竞猜结果");

		auto itr = onetoonegames.find(g_id);
		eosio_assert(itr != onetoonegames.end(),"该局游戏不存在");
		eosio_assert(itr->end != true,"已经开奖");
		eosio_assert(itr->player_num == 2,"人数未满");

		auto player_game_index = one2oneres.get_index<N(bygid)>();
		auto game_itr = player_game_index.find(g_id);
		while (game_itr != player_game_index.end() && game_itr->g_id == g_id) {
			if(game_itr->number == result) {
				// lotter 账户转账资产到赢的玩家
				action(permission_level{_self,N(active)},
					N(eosio.token),N(transfer),
					std::make_tuple(_self,game_itr->player_name,itr->bet * 2,std::string(""))
					).send();
				//获胜者和标记游戏已经结束
				onetoonegames.modify(itr,_self,[&](auto &g){
					g.win = game_itr->player_name;
					g.randseed = randseed;
					g.end = true;
				});
				break;
			}
			++game_itr;
		}
	}

	/**
	* 游戏一直未满，管理员主动结束，返回资金给竞猜者
	* @abi action 
	*/
	void stoppairgame(uint64_t g_id) {
		require_auth(_self);
		auto itr = onetoonegames.find(g_id);
		eosio_assert(itr != onetoonegames.end(),"该局游戏不存在");
		eosio_assert(itr->end == true,"已经开奖");

		auto pair_game_index = one2oneres.get_index<N(bygid)>();
		auto game_itr = pair_game_index.find(g_id);
		while(game_itr != pair_game_index.end() && game_itr->g_id == g_id) {

			//返还用户金额
			action(
				permission_level{_self,N(active)},
				N(eosio.token),N(transfer),
				std::make_tuple(_self,game_itr->player_name,game_itr->bet,string(""))
				).send();
			onetoonegames.modify(itr,_self,[&](auto &g){
				g.win = game_itr->player_name;
				g.randseed = 0;
				g.end = true;
			});
			++game_itr;
		}
	}

	/**
	* 开一局两人竞猜游戏，
	* account_name 
	*参数无意义，随便传
	*/
	void openpairgame(account_name name) {
		require_auth(_self);//只有lottery账户有权限开启一局新的游戏
		onetoonegames.emplace(_self,[&](auto &g){
			g.g_id = onetoonegames.available_primary_key();
			//g.date = now();
		});
	}


	/**支付失败的情况下从改局游戏中移除,这种情况不会存在
	* @abi action
	*/
	void removeplayer(uint64_t g_id,account_name name) {
		auto itr = games.find(g_id);
		eosio_assert(itr != games.end(),"该局游戏不存在");

		auto game_index = players.get_index<N(bygid)>();
		auto game_itr = game_index.find(g_id);
		while (game_itr != game_index.end() && game_itr->g_id == g_id) {
			auto player = players.find(game_itr->r_id);
			if(player->player_name == name) {
				eosio::print("删除玩家： ",eosio::name{name});
				players.erase(player);
				games.modify(itr,_self,[&](auto &g){
					g.current_index = g.current_index - 1;
				});
				break;
			}
			eosio::print("遍历出的玩家：",eosio::name{player->player_name} ,"玩家 id：",game_itr->r_id);
			++game_itr;
		}
	}
	

	private:

		enum GameType {dice_game,one_to_one_game,lotter_game};

		/**
		 *摇色子用户可押注类型
		 big,代表玩家压大()，small代表压小，leopard代表压大
		*/
		enum DiceDetainType {big,small,leopard};

		struct basegame {
			uint64_t g_id;
			uint64_t randseed;
			uint8_t end = false;//是否已经开奖
			time date = now();//开始游戏时间
			auto primary_key() const {return g_id;}

			EOSLIB_SERIALIZE(basegame,(g_id)(randseed)(end));
		};

		///@abi table lotterygame i64
		struct lotterygame:public basegame {

			uint8_t current_index;
			uint8_t max_palery;//本局玩家人数
			uint64_t pay;
			account_name win;

			EOSLIB_SERIALIZE(lotterygame,(g_id)(randseed)(end)(date)(current_index)(max_palery)(pay)(win));
		};

		///@abi table dicegame i64
		struct dicegame:public basegame {

			uint8_t one = 0;//开奖号码1
			uint8_t two = 0;//开奖号码2
			uint8_t three = 0;//开奖号码3
			uint32_t player_num;//本机摇色子人数限制
			uint32_t current_index = 0;
			account_name banker;//目前只支持我们做庄
			EOSLIB_SERIALIZE(dicegame,(g_id)(randseed)(end)(date)(one)(two)(three)(player_num)(current_index)(banker));
		};

		///@abi table onetoonegame i64
		struct onetoonegame:public basegame {

			asset bet;//赌注
			account_name win;
			uint8_t player_num = 0;

			EOSLIB_SERIALIZE(onetoonegame,(g_id)(randseed)(end)(date)(bet)(win)(player_num));
		};


		typedef eosio::multi_index<N(onetoonegame),onetoonegame> onetoonegame_index;
		typedef eosio::multi_index<N(dicegame),dicegame> dicegame_index;
		typedef eosio::multi_index<N(lotterygame),lotterygame> game_index;

		///@abi table player i64
		struct player {

			uint64_t r_id;
			account_name player_name;
			uint64_t g_id;
			uint64_t number;
			time date = now();
			auto primary_key() const {return r_id;}
			uint64_t game_id() const {return g_id;}

			EOSLIB_SERIALIZE(player,(r_id)(player_name)(g_id)(number)(date));
		};

		/// @abi table pairrecord i64
		struct pairrecord {
			uint64_t r_id;
			account_name player_name;
			uint64_t g_id;
			uint8_t number;
			asset bet;
			time date = now();
			auto primary_key() const {return r_id;}
			uint64_t game_id() const {return g_id;}

			EOSLIB_SERIALIZE(pairrecord,(r_id)(player_name)(g_id)(number)(bet)(date));
		};
		
		/// @abi table dicerecord i64
		struct dicerecord {
			uint64_t r_id;
			account_name player_name;
			uint64_t g_id;
			uint8_t detain_type;
			time date = now();
			asset bet;
			auto primary_key() const {return r_id;}
			uint64_t game_id() const {return g_id;}

			EOSLIB_SERIALIZE(dicerecord,(r_id)(player_name)(g_id)(detain_type)(bet)(date));

		};
		
		typedef eosio::multi_index<N(pairrecord),pairrecord,
		indexed_by<N(bygid),
		const_mem_fun<pairrecord,uint64_t,&pairrecord::game_id>>> pair_record_type;

		typedef eosio::multi_index<N(player),player,
		indexed_by<N(bygid),
		const_mem_fun<player,uint64_t,&player::game_id>>> palyer_table_type;

		typedef eosio::multi_index<N(dicerecord),dicerecord, 
		indexed_by<N(bygid), 
		const_mem_fun<dicerecord, uint64_t, &dicerecord::game_id>>> dice_record_type;
		

		void game_rule(uint64_t g_id) {
			auto game_index = players.get_index<N(bygid)>();
			auto game_itr = game_index.find(g_id);
			while(game_itr != game_index.end() && game_itr->g_id == g_id) {
				auto player = players.find(game_itr->r_id);
				eosio::print("本局游戏ID：", g_id,"玩家名: ",eosio::name{player->player_name},"该玩家竞猜数：",player->number);
				++game_itr;
			}
		}

		///检测是否为我们的货币
		void check_my_asset(const asset & quantity) {
			auto sy_n = S(4,RCC);
			eosio::print("传入是否为RCC格式：",sy_n);
			eosio_assert(quantity.symbol == sy_n,"只支持RCC的投注");
		}

		

		game_index games;
		dicegame_index dicegames;
		onetoonegame_index onetoonegames;
	
		palyer_table_type players;
		pair_record_type one2oneres;
		dice_record_type diceres;



};
EOSIO_ABI(lottery,(joindicegame)(opendicegame)(startdicegame)(stopdicegame)(join)(removeplayer)(start)(open)(openpairgame)(joinpair)(startpairgame)(stoppairgame))




