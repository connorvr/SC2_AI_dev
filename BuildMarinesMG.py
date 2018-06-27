# -*- coding: utf-8 -*-
"""
Created on Mon May  7 10:08:43 2018

@author: Connor

Based on the pysc2 tutorial by Steven Brown: https://github.com/skjb/pysc2-tutorial
"""

import random
import os.path

import numpy as np
import pandas as pd

from pysc2.agents import base_agent
from pysc2.lib import actions
from pysc2.lib import features


#cmd copy pasta
# python -m pysc2.bin.agent --map BuildMarines --agent BuildMarinesMG.SmartAgent --max_agent_steps 0 --norender


################
#Initialisation#
################
_NO_OP = actions.FUNCTIONS.no_op.id
_SELECT_POINT = actions.FUNCTIONS.select_point.id
_SELECT_IDLE_WORKER = actions.FUNCTIONS.select_idle_worker.id
_BUILD_SUPPLY_DEPOT = actions.FUNCTIONS.Build_SupplyDepot_screen.id
_BUILD_BARRACKS = actions.FUNCTIONS.Build_Barracks_screen.id
_TRAIN_MARINE = actions.FUNCTIONS.Train_Marine_quick.id
_TRAIN_SCV = actions.FUNCTIONS.Train_SCV_quick.id
_SELECT_ARMY = actions.FUNCTIONS.select_army.id
_ATTACK_MINIMAP = actions.FUNCTIONS.Attack_minimap.id
_HARVEST_GATHER = actions.FUNCTIONS.Harvest_Gather_screen.id

_PLAYER_RELATIVE = features.SCREEN_FEATURES.player_relative.index
_UNIT_TYPE = features.SCREEN_FEATURES.unit_type.index
_PLAYER_ID = features.SCREEN_FEATURES.player_id.index

_PLAYER_SELF = 1
_PLAYER_HOSTILE = 4
_ARMY_SUPPLY = 5

_TERRAN_COMMANDCENTER = 18
_TERRAN_SCV = 45 
_TERRAN_SUPPLY_DEPOT = 19
_TERRAN_BARRACKS = 21
_NEUTRAL_MINERAL_FIELD = 341

_NOT_QUEUED = [0]
_QUEUED = [1]
_SELECT_ALL = [2]

DATA_FILE = 'BuildMarine_agent_data'


ACTION_DO_NOTHING = 'donothing'
ACTION_SELECT_SCV = 'selectscv'
ACTION_BUILD_SUPPLY_DEPOT = 'buildsupplydepot'
ACTION_BUILD_BARRACKS = 'buildbarracks'
ACTION_SELECT_BARRACKS = 'selectbarracks'
ACTION_BUILD_MARINE = 'buildmarine'
ACTION_SELECT_CC = 'selectcommandcenter'
ACTION_BUILD_SCV = 'buildscv'
ACTION_SELECT_ARMY = 'selectarmy'
ACTION_ATTACK = 'attack'
ACTION_SELECT_IDLE_SCV = 'selectidlescv'
ACTION_GATHER_MINERALS = 'gatherminerals'

smart_actions = [
    ACTION_DO_NOTHING,
    ACTION_SELECT_SCV,
    ACTION_BUILD_SUPPLY_DEPOT,
    ACTION_BUILD_BARRACKS,
    ACTION_SELECT_BARRACKS,
    ACTION_BUILD_MARINE,
    ACTION_SELECT_CC,
    ACTION_BUILD_SCV,
    ACTION_SELECT_IDLE_SCV,
    ACTION_GATHER_MINERALS,
]


BUILT_MARINE_REWARD = 1

##########
#Q Matrix#
##########
class QLearningTable:
    def __init__(self, actions, learning_rate=0.01, reward_decay=0.9, e_greedy=0.9):
        self.actions = actions  # a list
        self.lr = learning_rate
        self.gamma = reward_decay
        self.epsilon = e_greedy
        self.q_table = pd.DataFrame(columns=self.actions, dtype=np.float64)
        self.disallowed_actions = {}

    def choose_action(self, observation, excluded_actions=[]):
        self.check_state_exist(observation)
        
        self.disallowed_actions[observation] = excluded_actions

        state_action = self.q_table.ix[observation, :]
        
        for excluded_action in excluded_actions:
            del state_action[excluded_action]
        
        if np.random.uniform() < self.epsilon:
            # some actions have the same value
            state_action = state_action.reindex(np.random.permutation(state_action.index))
            
            action = state_action.idxmax()
        else:
            action = np.random.choice(state_action.index)
            
        return action

    def learn(self, s, a, r, s_):
        self.check_state_exist(s_)
        self.check_state_exist(s)
        
        q_predict = self.q_table.ix[s, a]
        s_rewards = self.q_table.ix[s_, :]
        
        if s_ in self.disallowed_actions:
            for excluded_action in self.disallowed_actions[s_]:
                del s_rewards[excluded_action]
        
        q_target = r + self.gamma * s_rewards.max()
        
        # update
        self.q_table.ix[s, a] += self.lr * (q_target - q_predict)

    def check_state_exist(self, state):
        if state not in self.q_table.index:
            # append new state to q table
            self.q_table = self.q_table.append(pd.Series([0] * len(self.actions), index=self.q_table.columns, name=state))


#######
#Agent#
#######
class SmartAgent(base_agent.BaseAgent):
    
    def __init__(self):
        super(SmartAgent, self).__init__()
        
        self.qlearn = QLearningTable(actions=list(range(len(smart_actions))))
        
        self.previous_built_marine_score = 0
        
        self.previous_action = None
        self.previous_state = None
        
        self.previous_depot_placement = None
        self.previous_barracks_placement = None
        
        if os.path.isfile(DATA_FILE + '.gz'):
            self.qlearn.q_table = pd.read_pickle(DATA_FILE + '.gz', compression='gzip')
    
    def step(self, obs):
        super(SmartAgent, self).step(obs)
        
    
        if obs.last():
            #save Q table
            self.qlearn.q_table.to_pickle(DATA_FILE + '.gz', 'gzip')
            self.qlearn.q_table.to_csv(DATA_FILE + '.csv')
            
            self.previous_action = None
            self.previous_state = None
            

            return actions.FunctionCall(_NO_OP, [])
        
        
        #Count Buildings
        unit_type = obs.observation['screen'][_UNIT_TYPE]
        
        depot_y, depot_x = (unit_type == _TERRAN_SUPPLY_DEPOT).nonzero()
        supply_depot_count = int(round(len(depot_y) / 69))

        barracks_y, barracks_x = (unit_type == _TERRAN_BARRACKS).nonzero()
        barracks_count = int(round(len(barracks_x) / 100))
        
        #Get ingame values
        army_supply = obs.observation['player'][5]
        worker_count = obs.observation['player'][6]
        idle_worker_count = obs.observation['player'][7]
        
        if obs.observation['multi_select'].any():
            selected_unit = obs.observation['multi_select'][0][0]
        else:
            selected_unit = obs.observation['single_select'][0][0]
        
        
        #Define score
        built_marine_score = army_supply
        
        
        current_state = [  
            supply_depot_count,
            barracks_count,
            army_supply,
            worker_count,
            idle_worker_count,
            selected_unit,
        ]
        
        #Give rewards
        if self.previous_action is not None:
            reward = 0
                
            if built_marine_score > self.previous_built_marine_score:
                reward += BUILT_MARINE_REWARD
                
            self.qlearn.learn(str(self.previous_state), self.previous_action, reward, str(current_state))
        
        
        #Exclude actions from agent selection
        excluded_actions = []
        
        stop_depots = False
        if depot_y.any():
            if max(depot_y) >= 52 or supply_depot_count == 18:
                stop_depots = True

        if stop_depots and barracks_count >= 16 and idle_worker_count == 0:
            excluded_actions.append(1)
        if selected_unit != _TERRAN_SCV or stop_depots:
            excluded_actions.append(2)
        if supply_depot_count == 0 or selected_unit != _TERRAN_SCV or barracks_count == 16:
            excluded_actions.append(3)
        if barracks_count == 0:
            excluded_actions.append(4)
        if selected_unit != _TERRAN_BARRACKS or barracks_count == 0:
            excluded_actions.append(5)
        if worker_count > 26:
            excluded_actions.append(6)
        if selected_unit != _TERRAN_COMMANDCENTER or worker_count > 26:
            excluded_actions.append(7)
        if idle_worker_count == 0:
            excluded_actions.append(8)
        if selected_unit != _TERRAN_SCV or idle_worker_count == 0:
            excluded_actions.append(9)
    
    
        #Choose an action
        rl_action = self.qlearn.choose_action(str(current_state), excluded_actions)
        
        smart_action = smart_actions[rl_action]
        
        self.previous_built_marine_score = built_marine_score
        self.previous_state = current_state
        self.previous_action = rl_action
        
        #Define actions
        if smart_action == ACTION_SELECT_SCV:
            unit_type = obs.observation['screen'][_UNIT_TYPE]
            unit_y, unit_x = (unit_type == _TERRAN_SCV).nonzero()
                
            if unit_y.any():
                i = random.randint(0, len(unit_y) - 1)
                target = [unit_x[i], unit_y[i]]
                
                return actions.FunctionCall(_SELECT_POINT, [_NOT_QUEUED, target])
        
        elif smart_action == ACTION_BUILD_SUPPLY_DEPOT:
            if _BUILD_SUPPLY_DEPOT in obs.observation['available_actions']:
                previous_depot_placement = self.previous_depot_placement
                if supply_depot_count == 0:
                    target = [0, 5]

                elif previous_depot_placement[1] == 5:
                    indices = [i for i, j in enumerate(depot_y) if j ==previous_depot_placement[1]]
                    current_row = [depot_x[i] for i in indices]
                    if current_row == []:
                        target = previous_depot_placement
                    else:
                        target = [max(current_row)+3+np.random.binomial(5,0.2),previous_depot_placement[1]]
                
                else:
                    target = [0,max(depot_y)+3+np.random.binomial(5,0.2)]
                #x = [0,83] y = [5,62]        [int(x),int(y)]
                if target[0] > 83:
                    target = [0, target[1]+7]
                if target[1] > 62:
                    target = [0,5]
                previous_depot_placement = target
                
                self.previous_depot_placement = previous_depot_placement
                return actions.FunctionCall(_BUILD_SUPPLY_DEPOT, [_NOT_QUEUED, target])
        
        elif smart_action == ACTION_BUILD_BARRACKS:
            if _BUILD_BARRACKS in obs.observation['available_actions']:
                previous_barracks_placement = self.previous_barracks_placement
                if barracks_count == 0:
                    target = [0, 62]
                    
                else:
                    indices = [i for i, j in enumerate(barracks_y) if j == previous_barracks_placement[1]]
                    current_row = [barracks_x[i] for i in indices]
                    if current_row == []:
                        target = previous_barracks_placement
                    else:
                        target = [max(current_row)+6+np.random.binomial(5,0.2),previous_barracks_placement[1]]


                if target[0] > 83:
                    target = [46, previous_barracks_placement[1]-16]
                    if target[1] < 7:
                        target[1] = 62
                previous_barracks_placement = target
                self.previous_barracks_placement = previous_barracks_placement
                    
                #x = [0,83] y = [7,62]
                return actions.FunctionCall(_BUILD_BARRACKS, [_NOT_QUEUED, target])        

        
        elif smart_action == ACTION_SELECT_BARRACKS:
            if barracks_y.any():
                i = random.randint(0, len(unit_y) - 1)
                target = [unit_x[i], unit_y[i]]
                return actions.FunctionCall(_SELECT_POINT, [_SELECT_ALL, target])
        
        elif smart_action == ACTION_BUILD_MARINE:
            if _TRAIN_MARINE in obs.observation['available_actions']:
                return actions.FunctionCall(_TRAIN_MARINE, [_NOT_QUEUED])
            
        elif smart_action == ACTION_SELECT_CC:
            unit_type = obs.observation['screen'][_UNIT_TYPE]
            unit_y, unit_x = (unit_type == _TERRAN_COMMANDCENTER).nonzero()
                
            if unit_y.any():
                i = random.randint(0, len(unit_y) - 1)
                target = [unit_x[i], unit_y[i]]
                return actions.FunctionCall(_SELECT_POINT, [_NOT_QUEUED, target])
        
        elif smart_action == ACTION_BUILD_SCV:
            if _TRAIN_SCV in obs.observation['available_actions']:
                return actions.FunctionCall(_TRAIN_SCV, [_QUEUED])
            
        elif smart_action == ACTION_SELECT_IDLE_SCV:
            if _SELECT_IDLE_WORKER in obs.observation['available_actions']:
                return actions.FunctionCall(_SELECT_IDLE_WORKER, [_NOT_QUEUED])
            
        elif smart_action == ACTION_GATHER_MINERALS:
            if _HARVEST_GATHER in obs.observation['available_actions']:
                    unit_y, unit_x = (unit_type == _NEUTRAL_MINERAL_FIELD).nonzero()
                    
                    if unit_y.any():
                        i = random.randint(0, len(unit_y) - 1)
                        
                        m_x = unit_x[i]
                        m_y = unit_y[i]
                        
                        target = [int(m_x), int(m_y)]
                        
                        return actions.FunctionCall(_HARVEST_GATHER, [_QUEUED, target])
        return actions.FunctionCall(_NO_OP, [])
