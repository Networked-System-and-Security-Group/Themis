import os
import subprocess
from concurrent.futures import ProcessPoolExecutor

def execute_command(cmd):
    """Execute a shell command."""
    subprocess.run(cmd, shell=True, check=True)

def modify_config(base_config_path, new_config_path, xxx):
    """Modify the configuration file based on the given xxx value."""
    with open(base_config_path, 'r') as file:
        lines = file.readlines()

    lines[6] = f"FLOW_FILE mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}_1:10.txt\n"
    lines[7] = f"FCT_OUTPUT_FILE mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}/fct.txt\n"
    lines[8] = f"PFC_OUTPUT_FILE mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}/pfc.txt\n"
    lines[9] = f"TRACE_FILE mix/trace.txt\n"
    lines[10] = f"TRACE_OUTPUT_FILE mix/mix.tr\n"

    os.makedirs(os.path.dirname(new_config_path), exist_ok=True)
    with open(new_config_path, 'w') as file:
        file.writelines(lines)

def run_simulation(xxx):
    """Run the entire simulation pipeline for a given xxx value."""
    base_config_path = "./mix/config.txt"
    new_config_path = f"./mix/config_Ali_{xxx}.txt"

    # Step 1: Generate flow file
    flow_file_cmd = (
        f"python ../traffic_gen/pure_near_dst.py -c AliStorage2019.txt -n 32 -l {xxx} "
        f"-b 100G -t 0.03 -o ./mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}.txt"
    )
    # execute_command(flow_file_cmd)

    # Step 2: Trim the flow file
    trim_cmd = (
        f"python3 ../traffic_gen/trim-pure-dst.py -o "
        f"./mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}.txt"
    )
    # execute_command(trim_cmd)

    # Step 3: Modify the configuration file
    modify_config(base_config_path, new_config_path, xxx)

    # Step 4: Prepare the simulation command
    simulation_cmd = f"./waf --run 'scratch/third {new_config_path}'"
    return simulation_cmd

def main():
    xxx_values = [0.3, 0.5, 0.7]
    simulation_cmds = []

    # Run simulations sequentially to prepare commands
    for xxx in xxx_values:
        simulation_cmd = run_simulation(xxx)
        simulation_cmds.append(simulation_cmd)

    # Run simulations in parallel using ProcessPoolExecutor
    with ProcessPoolExecutor() as executor:
        executor.map(execute_command, simulation_cmds)

if __name__ == "__main__":
    main()