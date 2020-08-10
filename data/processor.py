from collections import namedtuple
import json
import os
import functools

Oprand = namedtuple('Oprand', ['id', 'size'])
Record = namedtuple('Record', ['name', 'ins', 'outs', 'workspace', 'time'])


def merge(function_path, timeline_path, pattern_path):
    # Read traced functions
    with open(function_path, 'r') as file:
        functions = json.load(file)
        codes = functions['code']
        data = functions['data']

    # Read timeline
    with open(timeline_path, 'r') as file:
        n = int(file.readline())
        _ = [file.readline() for _ in range(n)]
        times = {}
        for line in file.readlines():
            try:
                name, info = line.split(':')
                _, _, begin, end, _ = info.split(',')
                times[name] = (times[name] if times.get(name) else []) + [int(end) - int(begin)]
            except ValueError:
                # print('Error at line: {}'.format(line), end='')
                pass

    # Count operators
    op_counts = {}
    for code in codes:
        name = code['name']
        op_counts[name] = (op_counts[name] if op_counts.get(name) else 0) + 1

    # Checks
    for name, count in op_counts.items():
        if not name.startswith('.'):
            assert len(times[name]) % count == 0, name

    # Generator oprands
    oprands = []
    sizeof = {'Float32': 4, 'Int64': 8, 'Uint8': 1}
    for oprand in data:
        identity = oprand['id']
        size = functools.reduce(lambda x, y: x * y, oprand['shape']) * sizeof[oprand['type']] if oprand['shape'] else 0
        assert oprand['arch'] == 'CUDA'
        oprands.append(Oprand(identity, size))

    # Generate code
    average = lambda l: sum(l[1:]) / (len(l) - 1)
    indexing = {}
    records = []
    for code in codes:
        name = code['name']
        index = (indexing[name] if indexing.get(name) else 0)
        indexing[name] = index + 1
        record = Record(name, code['ins'], code['outs'],
                        0,  # TODO: add workspace
                        average(times[name][index::op_counts[name]]) if not name.startswith('.') else 0)
        records.append(record)

    # Write into file
    content = {'oprands': oprands, 'records': records}
    with open(pattern_path, 'w') as file:
        json.dump(content, file)


if __name__ == '__main__':
    for d in os.listdir():
        function_path = os.path.join(d, 'function.json')
        timeline_path = os.path.join(d, 'timeline.txt')
        if os.path.isdir(d) and \
                os.path.exists(function_path) and \
                os.path.exists(timeline_path):
            print('Merging model {} ... '.format(d), end='')
            merge(function_path, timeline_path, os.path.join(d, 'pattern.json'))
            print('done!')
