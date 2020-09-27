## DLMO: Deep Learning Memory Optimizer

### How to use

#### Step 1: Get traced function and profiling information

a. Trace:

```python
# `func` is the function to be traced 
traced_func = parrots.aot.trace(func, input, target)
```

b. Profiling:

```python
parrots.runtime.profile()
parrots.runtime.profile_memory()
```

c. Run it:

```python
loss, acc = traced_func(input, target)
```

d. Save IR and profiling information:

```python
parrots.aot.save(traced_func, 'function.json')
# Quit parrots and get `profile_memory.json` and `profile.txt`
```

#### Step 2: Generate merged IR JSON

a. Put original IR JSON `function.json`, timeline profiling result `profile.txt` (please rename to `timeline.txt`) and memory profiling result `profile_memory.json` (please rename to `memory.json`) into one directory under `data`, like:

```
├── data
│   ├── resnet152-32
│   │   ├── function.json
│   │   ├── memory.json
│   │   └── timeline.txt
│   ├── resnet50-32
│   │   ├── function.json
│   │   ├── memory.json
│   │   └── timeline.txt
│   ├── processor.py
```

b. Run `processor.py` (**maybe failed, please debug by yourself**)

```bash
python3 processor.py
```

c. You should get like this:

```
├── data
│   ├── resnet152-32
│   │   ├── function.json
│   │   ├── memory.json
│   │   ├── pattern.json
│   │   └── timeline.txt
│   ├── resnet50-32
│   │   ├── function.json
│   │   ├── memory.json
│   │   ├── pattern.json
│   │   └── timeline.txt
│   ├── processor.py
```

#### Step 3: Compile and Run!

a. Compile with CMake:

```bash
mkdir build
cd build

# Debug
cmake .. -DCMAKE_BUILD_TYPE=Debug
# Release (about 10x faster than debug binary)
cmake .. -DCMAKE_BUILD_TYPE=Release

make
cd ..
```

b. Create a `configs` file in `data` (one line with path and memory bound):

```
data/resnet50-32/pattern.json 1GiB
data/resnet152-32/pattern.json 2.1GiB
```

c. Run:

```
./build/dlmo data/configs
```

d. The output should be like (**you may have to read the source code and adjust the parameters**):

```
Running case data/resnet152-32/pattern.json (2356 operators) with optimizer (limit 1024.000000 MiB) ...
 > Start back-tracing search from source (peak memory: 8.019928 GiB, total time: 229.738614 ms)
 > Progress (300): 2.660553 GiB, 256.848531 ms
 > Progress (600): 1.800764 GiB, 287.683124 ms
 > Progress (900): 1.740950 GiB, 294.932769 ms
 > Progress (1200): 1.201523 GiB, 310.821406 ms
 > Reach search limit, stop searching
 > Result:
   > Schedules searched: 1500
   > Time used: 30178.606000 ms
   > Best: {peak memory: 1.158930 GiB, total time: 313.424069 ms}
   > Satisfy memory: false
 > Writing result into path best.json ... OK!
```

e. The newly generated IR JSON is named as `best.json`.
