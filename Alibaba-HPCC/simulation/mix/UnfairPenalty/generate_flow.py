

input_file='./Inter-DC/ExprGroup/flow2.txt'
oup_file='./Inter-DC/ExprGroup/flow.txt'
if __name__ == "__main__":
    #第一行为总数
    #其余行为src dst priority port flow_size measured_fct
    #将所有dst>=32的流量的行写入flow_measure.txt

    with open(input_file, 'r') as file:
        lines = file.readlines()
        with open(input_file, 'w') as file:
            for line in lines:
                if line[0] == 't':
                    continue
                dst = int(line.split()[0])
                if dst >= 32:
                    #输出到output文件
                    with open(oup_file, 'a') as file:
                        file.write(line)
