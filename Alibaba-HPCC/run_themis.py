import os
import subprocess

def execute_command(cmd):
    """Execute a shell command."""
    subprocess.run(cmd, shell=True, check=True)

def modify_config(base_config_path, new_config_path, xxx):
    """Modify the configuration file based on the given xxx value."""
    with open(base_config_path, 'r') as file:
        lines = file.readlines()

    lines[6] = f"FLOW_FILE mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}.txt\n"
    lines[7] = f"FCT_OUTPUT_FILE mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}/fct.txt\n"
    lines[8] = f"PFC_OUTPUT_FILE mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}/pfc.txt\n"
    lines[9] = f"TRACE_FILE mix/flow_Ali_{xxx}/trace.txt\n"
    lines[10] = f"TRACE_OUTPUT_FILE mix/flow_Ali_{xxx}/mix.tr\n"

    os.makedirs(os.path.dirname(new_config_path), exist_ok=True)
    with open(new_config_path, 'w') as file:
        file.writelines(lines)

def run_simulation(xxx):
        os.chdir("./simulation")
    """Run the entire simulation pipeline for a given xxx value."""
    base_config_path = "./mix/config.txt"
    new_config_path = f"./mix/config_Ali_{xxx}.txt"

    # Step 1: Generate flow file
    flow_file_cmd = (
        f"python ../traffic_gen/pure_near_dst.py -c AliStorage2019.txt -n 32 -l {xxx} "
        f"-b 100G -t 0.03 -o ./mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}.txt"
    )
    #execute_command(flow_file_cmd)

    # Step 2: Trim the flow file
    trim_cmd = (
        f"python3 ../traffic_gen/trim-pure-dst.py -o "
        f"./mix/UnfairPenalty/Inter-DC/ExprGroup/flow_Ali_{xxx}.txt"
    )
    #execute_command(trim_cmd)

    # Step 3: Modify the configuration file
    modify_config(base_config_path, new_config_path, xxx)

    # Step 4: Run the simulation

    simulation_cmd = f"./waf --run 'scratch/third {new_config_path}'"
    execute_command(simulation_cmd)
    os.chdir("..")

    os.chdir("./mix/UnfairPenalty")

if __name__ == "__main__":
    xxx_values = [0.3, 0.5, 0.7]

    # Run simulations sequentially
    for xxx in xxx_values:
        run_simulation(xxx)
