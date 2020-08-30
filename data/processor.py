from collections import namedtuple
import json
import os
import functools

Operand = namedtuple('Operand', ['id', 'size'])
Record = namedtuple('Record', ['name', 'ins', 'outs', 'workspace', 'time'])


def same(l):
    x = l[0]
    for y in l[1:]:
        if x != y:
            return False
    return True


def merge(function_path, timeline_path, memory_path, pattern_path):
    # Read traced functions
    with open(function_path, 'r') as file:
        functions = json.load(file)
        codes = functions['code']
        data = functions['data']
        first_code, last_code = None, None
        for code in codes:
            if code['name'].startswith('.'):
                continue
            if not first_code:
                first_code = code['name']
            last_code = code['name']

    # Read timeline
    with open(timeline_path, 'r') as file:
        n = int(file.readline())
        cuda_streams = []
        for i in range(n):
            line = file.readline()
            if line.count('Cuda') > 0:
                stream_id, _ = line.split(',')
                cuda_streams.append(stream_id)
        times = {}
        lines = file.readlines()
        for i in range(len(lines)):
            if lines[i].count(first_code):
                lines = lines[i:]
                break
        for i in range(len(lines) - 1, -1, -1):
            if lines[i].count(last_code):
                lines = lines[:i + 1]
                break
        for line in lines:
            is_cuda = False
            for cuda_stream in cuda_streams:
                if line.count(cuda_stream):
                    is_cuda = True
                    break
            if not is_cuda:
                continue
            try:
                name, info = line.split(':')
                _, _, begin, end, _ = info.split(',')
                times[name] = (times[name] if times.get(name) else []) + [int(end) - int(begin)]
            except ValueError:
                # print('Error at line: {}'.format(line), end='')
                pass

    # Read memory
    workspaces = {}
    with open(memory_path, 'r') as file:
        records = json.load(file)['records']
        for record in records:
            name = record['name']
            workspaces[name] = (workspaces[name] if workspaces.get(name) else []) + [record['workspaceUsage']]

    # Count operators
    op_counts = {}
    for code in codes:
        name = code['name']
        op_counts[name] = (op_counts[name] if op_counts.get(name) else 0) + 1

    # Checks
    for name, count in op_counts.items():
        if not name.startswith('.'):
            assert len(times[name]) % count == 0, name
            assert len(workspaces[name]) % count == 0, name
            # print(name, count, len(workspaces[name]), len(times[name]))

    # Generator operands
    operands = []
    sizeof = {'Float32': 4, 'Int64': 8, 'Uint8': 1}
    for operand in data:
        identity = operand['id']
        if not operand['type']:
            # TODO: fix it
            size = 0
        else:
            size = (functools.reduce(lambda x, y: x * y, operand['shape']) if operand['shape'] else 1) * \
                   sizeof[operand['type']]
        assert operand['arch'] == 'CUDA'
        operands.append(Operand(identity, size))

    # Generate code
    average = lambda l: sum(l[2:]) / (len(l) - 2)
    indexing = {}
    records = []
    last_name = ''
    for code in codes:
        name = code['name']
        index = (indexing[name] if indexing.get(name) else 0)
        indexing[name] = index + 1
        if not same(workspaces[name][index::op_counts[name]]):
            # TODO: fix it
            # print(code, workspaces[name][index::op_counts[name]])
            pass
        if name == 'reshape':
            assert last_name == '.share'
        else:
            record = Record(name, code['ins'], code['outs'],
                        workspaces[name][index + op_counts[name]],  # TODO: temporarily choose the second
                        average(times[name][index::op_counts[name]]) if not name.startswith('.') else 0)
            records.append(record)
        last_name = name

    # Write into file
    content = {'operands': operands, 'records': records}
    with open(pattern_path, 'w') as file:
        json.dump(content, file)


if __name__ == '__main__':
    for d in os.listdir():
        function_path = os.path.join(d, 'function.json')
        timeline_path = os.path.join(d, 'timeline.txt')
        memory_path = os.path.join(d, 'memory.json')
        if os.path.isdir(d) and \
                os.path.exists(function_path) and \
                os.path.exists(timeline_path):
            print('Merging model {} ... '.format(d), end='')
            merge(function_path, timeline_path, memory_path, os.path.join(d, 'pattern.json'))
            print('done!')
