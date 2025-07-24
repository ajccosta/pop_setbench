import sys
from collections import defaultdict

allocators = ["deqalloc", "mimalloc"]

def is_number(s):
    try:
        int(s)
        return True
    except ValueError:
        return False

def parse_and_average(filename):
    results = defaultdict(lambda: defaultdict(list))
    current_benchmark = None

    with open(filename) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            # Benchmark header (no space, no digit at start)
            if not line[0].isdigit() and ' ' not in line:
                current_benchmark = line
                continue
            # Skip if benchmark not yet set
            if current_benchmark is None:
                continue
            # Split and validate
            parts = line.split()
            if len(parts) != 2 or not is_number(parts[1]):
                continue
            system, val = parts
            if system not in allocators:
                allocators.append(system)
            results[current_benchmark][system].append(int(val))

    # Print results
    result_comparison = {}
    for benchmark, systems in results.items():
        result_comparison[benchmark] = {}
        print(f"Benchmark: {benchmark}")
        best_other_system = None #best system excluding deqalloc
        best_avg = 0
        for system in allocators:
            values = systems.get(system, [])
            if not values:
                continue
            avg = sum(values) // len(values)
            print(f"  {system}: {avg}")
            result_comparison[benchmark][system] = avg
            if (best_other_system == None or best_avg < avg) and system != 'deqalloc':
                best_other_system = system
                best_avg = avg
        improvement=round(((result_comparison[benchmark]['deqalloc'])/(result_comparison[benchmark][best_other_system])-1)*100,5)
        print(f"  improvement over best other system ({best_other_system}): {improvement}%")
        print()

# Example usage
parse_and_average(sys.argv[1])