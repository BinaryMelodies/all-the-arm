#! /usr/bin/python3

# Process the instruction set database and generating code for disassembly and execution

import os
import re
import sys

db = {}
current_isa = None
is_begin_block = False

VERSION_LIST = ['1', '2', '3', '4', '5', '6', '7', '8', '8.1', '8.2', '8.3']
COPROC_VERSION_LIST = ['FPA', 'VFPv1', 'VFPv2', 'VFPv3', 'VFPv4', 'VFPv5', '8'] # adding '8' is a hack to make certain checks happy
COPROC_VERSION_END = {'FPA': 'FPA', 'VFPv1': '8', 'VFPv2': '8', 'VFPv3': '8', 'VFPv4': '8', 'VFPv5': '8', '8': '8'}

VERSION_NAMES = {
	'1':   'ARMV1',
	'2':   'ARMV2',
	'3':   'ARMV3',
	'4':   'ARMV4',
	'5':   'ARMV5',
	'6':   'ARMV6',
	'7':   'ARMV7',
	'8':   'ARMV8',
	'8.1': 'ARMV81',
	'8.2': 'ARMV82',
	'8.3': 'ARMV83',
	'9':   'ARMV9',
}

COPROC_VERSION_NAMES = {
	'VFPv1': 'ARM_VFPV1',
	'VFPv2': 'ARM_VFPV2',
	'VFPv3': 'ARM_VFPV3',
	'VFPv4': 'ARM_VFPV4',
	'VFPv5': 'ARM_VFPV5',
#	'8':     'ARM_V8FP', # not actually used, we check the CPU version
}

FEATURE_LIST = {
	'a': "FEATURE_SWP",
	'G': "FEATURE_ARM26",
	'M': "FEATURE_MULL",
	'T': "FEATURE_THUMB",
	'E': "FEATURE_ENH_DSP",
	'P': "FEATURE_DSP_PAIR",
	'J': "FEATURE_JAZELLE",
	'K': "FEATURE_MULTIPROC",
	'T2': "FEATURE_THUMB2",
	'Z': "FEATURE_SECURITY",
	'VE': "FEATURE_VIRTUALIZATION",

	'VFP': "FEATURE_VFP",
	'D': "FEATURE_DREG",
	'D32': "FEATURE_32_DREG",
	'fp16': "FEATURE_FP16",
	'SIMD': "FEATURE_SIMD",
	'SIMD+VFP': ("FEATURE_SIMD", "FEATURE_VFP"),
	'SIMD+fp16': ("FEATURE_SIMD", "FEATURE_FP16"),
}

VERSION_TYPES = {
	# present in all processors
	'1': {
		'type': 'main',
		'a': {
			'version': '1',
			'feature': (),
		},
	},

	# introduced in ARMv2
	'2': {
		'type': 'main',
		'a': {
			'version': '2',
			'feature': (),
		},
	},

	# introduced/removed in ARMv2a
	'2a': {
		'type': 'main',
		'a': {
			'version': '2',
			'feature': ('a',), # SWP
		},
	},

	# introduced in ARMv3
	'3': {
		'type': 'main',
		'a': {
			'version': '3',
			'feature': (),
		},
	},

	# introduced in ARMv3M, available in ARMv4 and ARMv5 and later, except for ARMv4xM, ARMv4TxM, ARMv5xM, ARMv5TxM
	'3M': {
		'type': 'main',
		'a': {
			'version': '3',
			'feature': ('M',), # long multiplication
		},
	},

	# introduced in ARMv4
	'4': {
		'type': 'main',
		'a': {
			'version': '4',
			'feature': (),
		},
	},

	# introduced in ARMv4T, available in ARMv5T and ARMv6 and later
	'4T': {
		'type': 'main',
		'a': {
			'version': '4',
			'feature': ('T',), # introduced in Thumb
		},
	},

	# introduced in ARMv5
	'5': {
		'type': 'main',
		'a': {
			'version': '5',
			'feature': (),
		},
	},

	# introduced in ARMv5T, available in ARMv6 and later
	'5T': {
		'type': 'main',
		'a': {
			'version': '5',
			'feature': ('T',), # introduced in Thumb
		},
	},

	# introduced in ARMv5TE, available in ARMv6 and later
	'5TExP': {
		'type': 'main',
		'a': {
			'version': '5',
			'feature': ('E',), # Enhanced DSP
		},
	},

	# introduced in ARMv5TE, except for ARMv5TExP, available in ARMv6 and later
	'5TE': {
		'type': 'main',
		'a': {
			'version': '5',
			'feature': ('P',), # Enhanced DSP, not available on 5TExP
		},
	},

	# introduced in ARMv5TEJ, available in ARMv6 and later
	'5TEJ': {
		'type': 'main',
		'a': {
			'version': '5',
			'feature': ('J',), # introduced in Jazelle
		},
	},

	# introduced in ARMv6
	'6': {
		'type': 'main',
		'a': {
			'version': '6',
			'feature': (),
		},
	},

	# introduced in ARMv6, unavailable in M-Profile (ARMv6-M, ARMv7-M and later)
	'6, except 6-M': {
		'type': 'main',
		'a': {
			'version': '6',
			'feature': (),
		},
		'm': None,
	},

	# introduced in ARMv6K, available in ARMv7 and later
	'6K': {
		'type': 'main',
		'a': {
			'version': '6',
			'feature': ('K',), # multiprocessing
		},
	},

	# introduced/replaced in ARMv6T2, available in ARMv7 and later
	'6T2': {
		'type': 'main',
		'a': {
			'version': '6',
			'feature': ('T2',),
		},
		#'m': { 'version': '7', },
	},

	# introduced in ARMv6T2, available in ARMv7 and later, except for M-Profile (ARMv7-M)
	'6T2, except 7-M': {
		'type': 'main',
		'a': {
			'version': '6',
			'feature': ('T2',),
		},
		'm': None,
	},

	# introduced in ARMv6T2 as well as ARMv6K, available in ARMv7 and later
	'6T2, 6K': {
		'type': 'main',
		'a': {
			'version': '6',
			'feature': ('T2', 'K'), # introduced in Thumb-2 as well as multiprocessing extensions
		},
		#'m': { 'version': '7', },
	},

	# introduced in ARMv6, also available in M-Profile (ARMv6-M, ARMv7-M and later)
	'6T2, 6-M': {
		'type': 'main',
		'a': {
			'version': '6',
			'feature': ('T2',),
		},
		'm': {
			'version': '6',
			'feature': (),
		},
	},

	# introduced in ARMv6T2, available in ARMv7E-M and later
	'6T2, 7E-M': {
		'type': 'main',
		'a': {
			'version': '6',
			'feature': ('T2',),
		},
		#'m': { 'version': '7', 'feature': ('E',) }, # TODO
	},

	# introduced in ARMv7, also available in M-Profile (ARMv6-M and later)
	'6-M, 7': {
		'type': 'main',
		'a': {
			'version': '7',
			'feature': (),
		},
		'm': {
			'version': '6',
			'feature': (),
		},
	},

	# introduced in ARMv7
	'7': {
		'type': 'main',
		'a': {
			'version': '7',
			'feature': (),
		},
	},

	# introduced in ARMv7 Multiprocessing Extensions
	'7+mp': {
		'type': 'main',
		'a': {
			'version': '7',
			'feature': ('K',), # Multiprocessing Extensions
		},
	},

	# introduced in ARMv7-M, available in M-Profile processors (ARMv8-M and later)
	'7-M': {
		'type': 'main',
		'm': {
			'version': '7',
			'feature': (),
		},
	},

	# introduced in ARMv7VE, available in ARMv8 and later
	'7VE': {
		'type': 'main',
		'a': {
			'version': '7',
			'feature': ('VE',), # introduced with Virtualization Extensions
		},
	},

	# introduced in ARMv8
	'8': {
		'type': 'main',
		'a': {
			'version': '8',
			'feature': (),
		},
	},

	# introduced in ARMv8, half precision only
	'8+hp': {
		'type': 'main',
		'a': {
			'version': '8',
			'feature': ('fp16',),
		},
	},

	# introduced in ARMv8-R, R-Profile only
	'8-R': {
		'type': 'main',
		'r': {
			'version': '8',
			'feature': (),
		},
	},

	# introduced in ARMv8.1
	'8.1': {
		'type': 'main',
		'a': {
			'version': '8.1',
			'feature': (),
		},
	},

	# introduced in ARMv8.2
	'8.2': {
		'type': 'main',
		'a': {
			'version': '8.2',
			'feature': (),
		},
	},

	# introduced in ARMv8.3
	'8.3': {
		'type': 'main',
		'a': {
			'version': '8.3',
			'feature': (),
		},
	},

	# FPA coprocessor instruction
	'FPA': {
		'type': 'coproc',
		'a': {
			'version': 'FPA',
			'feature': (),
		},
	},

	# introduced in VFPv1
	'VFPv1': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv1',
			'feature': ('VFP',),
		},
	},

	# introduced in VFPv1, only available if 64-bit floating point is supported
	'VFPv1D': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv1',
			'feature': ('D',), # TODO: double precision, 16 registers
		},
	},

	# introduced in VFPv1, also available if Advanced SIMD is supported
	'VFPv1, AdvSIMDv1': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv1',
			'feature': ('VFP', 'SIMD'),
		},
	},

	# introduced in VFPv1, only available if 64-bit floating point or Advanced SIMD is supported
	'VFPv1D, AdvSIMDv1': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv1',
			'feature': ('D', 'SIMD'),
		},
	},

	# introduced in VFPv2
	'VFPv2': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv2',
			'feature': ('VFP',),
		},
	},

	# introduced in VFPv2, also available if Advanced SIMD is supported
	'VFPv2, AdvSIMDv1': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv2',
			'feature': ('VFP', 'SIMD'),
		},
	},

	# introduced in VFPv3
	'VFPv3': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv3',
			'feature': ('VFP',),
		},
	},

	# introduced in VFPv3, requires double precision
	'VFPv3D': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv3',
			'feature': ('D',),
		},
	},

	'VFPv3+fp16': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv3',
			'feature': ('fp16',),
		},
	},

	'VFPv4': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv4',
			'feature': ('VFP',),
		},
	},

	'VFPv5': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv5',
			'feature': ('VFP',),
		},
	},

	'VFPv5+dp': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv5',
			'feature': ('D',),
		},
	},

	'AdvSIMDv1': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv3',
			'feature': ('SIMD',),
		},
	},

	'AdvSIMDv1+sp': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv3',
			'feature': ('SIMD+VFP',),
		},
	},

	'AdvSIMDv1+fp16': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv3',
			'feature': ('SIMD+fp16',),
		},
	},

	'AdvSIMDv2+sp': {
		'type': 'coproc',
		'a': {
			'version': 'VFPv4',
			'feature': ('SIMD+VFP',),
		},
	},
}

FEATURES = {
	'sp': 'VFP',
	'dp': 'D',
	'D32': 'D32',
	'hp': 'fp16',
}

# these are processed as if they were separate instruction sets, but they are just coprocessor operation specializations
COPROC_ISAS = {'cdp', 'ldc', 'mcr', 'mcrr', 'mrc', 'mrrc'}

######## Process command line arguments

DAT_FILE = None
PARSE_FILE = None
STEP_FILE = None
HTML_FILE = None
i = 1
while i < len(sys.argv):
	if sys.argv[i] == '-d':
		i += 1
		DAT_FILE = sys.argv[i]
	elif sys.argv[i] == '-p':
		i += 1
		PARSE_FILE = sys.argv[i]
	elif sys.argv[i] == '-s':
		i += 1
		STEP_FILE = sys.argv[i]
	elif sys.argv[i] == '-h':
		i += 1
		HTML_FILE = sys.argv[i]
	elif DAT_FILE is None:
		DAT_FILE = sys.argv[i]
	else:
		assert False
	i += 1

######## Read in the database file

with open(DAT_FILE, 'r') as file:
	for line_number, line in enumerate(file):
		if is_begin_block:
			if line.rstrip() == 'end':
				is_begin_block = False
			else:
				db[current_isa][-1]['begin'].append(line.rstrip())
			continue
		line = line.strip()
		if line == '':
			continue
		if line.startswith('#'):
			continue
		if '\t' in line:
			op = line[:line.find('\t')]
			param = line[line.find('\t') + 1:]
		else:
			op = line
			param = None

		if op == 'isa':
			current_isa = param
			assert current_isa in {'A32', 'T32', 'J32', 'A64', 'coproc', 'A32+T32'}
			if current_isa not in db:
				db[current_isa] = []
		elif op == 'code':
			db[current_isa].append({'code': param, '#': line_number + 1})
		elif op == 'code.a' or op == 'code.t':
			assert current_isa == 'A32+T32'
			other_op = 'code.a' if op != 'code.a' else 'code.t'
			if len(db[current_isa]) > 0 and op not in db[current_isa][-1] and other_op in db[current_isa][-1]:
				db[current_isa][-1][op] = param
			else:
				db[current_isa].append({op: param, '#': line_number + 1})
		elif op in {'mcr', 'mrc', 'mcrr', 'mrrc'}:
			db[current_isa].append({'abbreviation': (op, param), '#': line_number + 1})
		elif op == 'asm':
			db[current_isa][-1]['asm'] = param
		elif op == 'asm.ual':
			assert current_isa == 'A32' or current_isa == 'T32' or current_isa == 'coproc'
			db[current_isa][-1]['ual'] = param
		elif op == 'it':
			assert current_isa == 'T32'
			db[current_isa][-1]['it'] = param
		elif op == 'thumbee':
			assert current_isa == 'T32'
			db[current_isa][-1]['thumbee'] = param
		elif op == 'added':
			db[current_isa][-1]['added'] = param
		elif op == 'removed':
			db[current_isa][-1]['removed'] = param
		elif op == 'a26':
			assert current_isa == 'A32'
			db[current_isa][-1]['a26'] = param
		elif op == 'begin':
			db[current_isa][-1]['begin'] = [line_number]
			is_begin_block = True
		elif op == 'exclude':
			if 'exclude' not in db[current_isa][-1]:
				db[current_isa][-1]['exclude'] = []
			db[current_isa][-1]['exclude'].append(param)
		elif op == 'match':
			if 'match' not in db[current_isa][-1]:
				db[current_isa][-1]['match'] = []
			db[current_isa][-1]['match'].append(param)
		elif op == 'when':
			if 'when' not in db[current_isa][-1]:
				db[current_isa][-1]['when'] = []
			db[current_isa][-1]['when'].append(param)
		elif op == 'coproc':
			assert current_isa in {'A32', 'T32'}
			assert param in COPROC_ISAS
			db[current_isa][-1]['coproc'] = param
		elif op == 'usedby':
			assert current_isa == 'J32'
			db[current_isa][-1]['usedby'] = param
		else:
			print(op)
			assert False

INS_LINE = None
def ins_assert(condition, message = ''):
	global INS_LINE
	if not condition:
		print("Line", INS_LINE, message, file = sys.stderr)
		assert condition

# Preprocessing: coprocessor registers
for ins_set in db.values():
	for ins in ins_set:
		INS_LINE = ins['#']
		if 'abbreviation' in ins:
			kind, param = ins['abbreviation']
			del ins['abbreviation']
			if kind == 'mcr' or kind == 'mrc':
				match = re.match('p([0-9]+), ([0-9]+), c([0-9]+), c([0-9]+), ([0-9]+)', param)
				cp = int(match[1])
				op1 = int(match[2])
				cr1 = int(match[3])
				cr2 = int(match[4])
				op2 = int(match[5])
				if kind == 'mrc':
					ins['code'] = f'!!!@1110{op1:03b}1{cr1:04b}....{cp:04b}{op2:03b}1{cr2:04b}'
				elif kind == 'mcr':
					ins['code'] = f'!!!@1110{op1:03b}0{cr1:04b}....{cp:04b}{op2:03b}1{cr2:04b}'
			# TODO: mcrr, mrrc
			else:
				ins_assert(False)

# Preprocessing: Advanced SIMD
for ins in db['A32+T32']:
	ins1 = ins.copy()
	ins1['code'] = ins1['code.a']
	del ins1['code.a']
	del ins1['code.t']
	if 'exclude' in ins1:
		ins1['exclude'] = ins1['exclude'].copy()
		i = 0
		while i < len(ins1['exclude']):
			if ins1['exclude'][i].endswith('\tfor\tThumb'):
				del ins1['exclude'][i]
			else:
				if '\tfor\t' in ins1['exclude'][i]:
					ins1['exclude'][i] = ins1['exclude'][i].split('\tfor\t')[0]
				i += 1
	db['A32'].append(ins1)

	ins2 = ins.copy()
	ins2['code'] = ins2['code.t'][:16] + ' ' + ins2['code.t'][16:]
	del ins2['code.a']
	del ins2['code.t']
	if 'exclude' in ins2:
		ins2['exclude'] = ins2['exclude'].copy()
		i = 0
		while i < len(ins2['exclude']):
			if ins2['exclude'][i].endswith('\tfor\tARM'):
				del ins2['exclude'][i]
			else:
				if '\tfor\t' in ins2['exclude'][i]:
					ins2['exclude'][i] = ins2['exclude'][i].split('\tfor\t')[0]
				ins2['exclude'][i] = ins2['exclude'][i][:16] + ' ' + ins2['exclude'][i][16:]
				i += 1
	db['T32'].append(ins2)

# Preprocessing:
# expand exclude/before structures
for current_isa in db.keys():
	i = 0
	while i < len(db[current_isa]):
		ins = db[current_isa][i]
		divides = set()
		for exclude in ins.get('exclude', ()):
			if '\tbefore\t' in exclude:
				divides.add(exclude.split('\tbefore\t')[1])
			if '\tsince\t' in exclude:
				divides.add(exclude.split('\tsince\t')[1])

		if len(divides) > 0:
			assert len(divides) == len({divide[0] for divide in divides})
			added = None
			for removed in sorted(divides) + [None]:
				ins0 = ins.copy()
				if added is not None:
					ins0['added'] = added
				if removed is not None:
					ins0['removed'] = removed
				ins0['exclude'] = ins['exclude'].copy()
				j = 0
				while j < len(ins0['exclude']):
					exclude = ins0['exclude'][j]
					if '\tbefore\t' in exclude:
						exclude, before = exclude.split('\tbefore\t')
						if removed is None or removed > before:
							ins0['exclude'].pop(j)
							continue
						else:
							ins0['exclude'][j] = exclude
					elif '\tsince\t' in exclude:
						exclude, since = exclude.split('\tsince\t')
						if removed is not None and removed <= since:
							ins0['exclude'].pop(j)
							continue
						else:
							ins0['exclude'][j] = exclude
					j += 1

				if 'when' in ins0:
					ins0['when'] = ins0['when'].copy()

				if added is None:
					db[current_isa][i] = ins0
				else:
					db[current_isa].insert(i, ins0)
				added = removed
				i += 1
		else:
			i += 1

def extract_mask(mode, code, variant = None):
	varfields = {}
	mask1 = value1 = 0
	mask2 = value2 = 0

	if mode == 't16' or mode == 't32':
		WORDBITS = 16
	else:
		WORDBITS = 32

	for i in range(WORDBITS):
		c = code[WORDBITS - 1 - i]
		if c == '0' or (variant == 'string' and c == '@'):
			mask1 |= 1 << i
		elif c == '1' or (variant == 'string' and c == '!'):
			mask1 |= 1 << i
			value1 |= 1 << i
		elif c == '@' or c == '!' or c == '.':
			pass
		else:
			if c not in varfields:
				varfields[c] = []

			if len(varfields[c]) == 0 or varfields[c][-1][-2:] != [i - 1, 0]:
				varfields[c].append([i, i, 0])
			else:
				varfields[c][-1][1] = i

	if mode == 't32':
		varfields2 = {}
		for i in range(16):
			c = code[16 + 1 + 15 - i]
			if c == '0':
				mask2 |= 1 << i
			elif c == '1':
				mask2 |= 1 << i
				value2 |= 1 << i
			elif c == '@' or c == '!' or c == '.':
				pass
			else:
				if c not in varfields2:
					varfields2[c] = []

				if len(varfields2[c]) == 0 or varfields2[c][-1][-2:] != [i - 1, 1]:
					varfields2[c].append([i, i, 1])
				else:
					varfields2[c][-1][1] = i

		for c in varfields2:
			if c in varfields:
				varfields[c] = varfields2[c] + varfields[c]
			else:
				varfields[c] = varfields2[c]

	return varfields, mask1, value1, mask2, value2

# expand when conditions
for current_isa in db.keys():
	i = 0
	while i < len(db[current_isa]):
		ins = db[current_isa][i]
		if 'when' not in ins:
			i += 1
			continue

		when = ins['when'].pop().split('\t')
		if len(ins['when']) == 0:
			del ins['when']
		pattern = when.pop(0)

		if 'exclude' in ins:
			if current_isa == 'T32':
				mode = 't32' if ' ' in ins['code'] else 't16'
			else:
				mode = current_isa.lower()

			include = True
			if 'exclude' in ins:
				_, mask1, value1, mask2, value2 = extract_mask(mode, pattern)

				for ex in ins['exclude']:
					_, x_mask1, x_value1, x_mask2, x_value2 = extract_mask(mode, ex)
					if (value1 & x_mask1) != (x_value1 & mask1):
						include = False
						break
					elif mode == 't32' and (value2 & x_mask2) != (x_value2 & mask2):
						include = False
						break

			keyword = when.pop(0)
			value = '\t'.join(when)

			if include:
				ins1 = ins.copy()
			else:
				ins1 = ins

			if 'exclude' not in ins1:
				ins1['exclude'] = []
			else:
				ins1['exclude'] = ins1['exclude'].copy()
			ins1['exclude'].append(pattern)
			db[current_isa][i] = ins1

			if include:
				ins0 = ins.copy()
				if 'match' not in ins0:
					ins0['match'] = []
				else:
					ins0['match'] = ins0['match'].copy()
				ins0['match'].append(pattern)
				ins0[keyword] = value
				db[current_isa].insert(i + 1, ins0)

for ins in db['A32']:
	INS_LINE = ins['#']
	ins_assert('code' in ins)
	ins_assert('asm' in ins)
	ins_assert('added' in ins)
	ins_assert(len(ins['code']) == 32)
	if 'begin' not in ins:
		print(f"Warning: no definition for A32 line {INS_LINE}", file = sys.stderr)

for ins in db['T32']:
	INS_LINE = ins['#']
	ins_assert('code' in ins)
	ins_assert('asm' in ins)
	ins_assert('added' in ins)
	ins_assert(len(ins['code']) == 16 or len(ins['code']) == 33)
	if 'begin' not in ins:
		print(f"Warning: no definition for T32 line {INS_LINE}", file = sys.stderr)

for ins in db['J32']:
	INS_LINE = ins['#']
	ins_assert('code' in ins)
	ins_assert('asm' in ins)
	ins_assert('added' in ins)
	if 'begin' in ins:
		ins_assert('usedby' in ins)
	#if 'begin' not in ins:
	#	print(f"Warning: no definition for J32 line {INS_LINE}", file = sys.stderr)

for ins in db['A64']:
	INS_LINE = ins['#']
	ins_assert('code' in ins)
	ins_assert('asm' in ins)
	ins_assert('added' in ins)
	ins_assert(len(ins['code']) == 32)
	if 'begin' not in ins:
		print(f"Warning: no definition for A64 line {INS_LINE}", file = sys.stderr)

# Distribute coprocessor instructions into groups

# CDP
#	cccc1110aaaannnnddddCCCCbbb0mmmm
# LDC/STC
#	cccc1101UNWLnnnnddddCCCCoooooooo
#	cccc1100UN1LnnnnddddCCCCoooooooo
#	cccc11001N0LnnnnddddCCCCoooooooo
# MCR
#	cccc1110aaa0nnnnddddCCCCbbb1mmmm
# MRC
#	cccc1110aaa1nnnnddddCCCCbbb1mmmm
# MCRR
#	cccc11000100TTTTttttCCCCaaaammmm
# MRRC
#	cccc11000101TTTTttttCCCCaaaammmm

#	....11000100.................... MCRR
#	....11000101.................... MRRC
#	....110......................... LDC/STC
#	....1110...................0.... CDP
#	....1110...0...............1.... MCR
#	....1110...1...............1.... MRC

#	....11000100.................... MCRR
#	....11000101.................... MRRC
#	....110......................... LDC/STC
#	....1110...................0.... CDP
#	....1110...0...............1.... MCR
#	....1110...1...............1.... MRC

if 'CDP' not in db:
	db['CDP'] = []
if 'LDC' not in db:
	db['LDC'] = []
if 'MCR' not in db:
	db['MCR'] = []
if 'MRC' not in db:
	db['MRC'] = []
if 'MCRR' not in db:
	db['MCRR'] = []
if 'MRRC' not in db:
	db['MRRC'] = []

for ins in db['coproc']:
	INS_LINE = ins['#']
	ins_assert('code' in ins)
	#ins_assert('asm' in ins)
	ins_assert('added' in ins)
	ins_assert(ins['code'][:4] == '1111' or ('0' not in ins['code'][:4] and '1' not in ins['code'][:4]))
	if ins['code'][4:12] == '11000100':
		db['MCRR'].append(ins)
	elif ins['code'][4:12] == '11000101':
		db['MRRC'].append(ins)
	elif ins['code'][4:7] == '110':
		# P, U, W cannot all be 0
		ins_assert(ins['code'][7] != '0' or ins['code'][8] != '0' or ins['code'][10] != '0')
		db['LDC'].append(ins)
	elif ins['code'][4:8] == '1110' and ins['code'][27] == '0':
		db['CDP'].append(ins)
	elif ins['code'][4:8] == '1110' and ins['code'][27] == '1' and ins['code'][11] == '0':
		db['MCR'].append(ins)
	elif ins['code'][4:8] == '1110' and ins['code'][27] == '1' and ins['code'][11] == '1':
		db['MRC'].append(ins)
	else:
		ins_assert(False)

def get_proc_version(version_data, version_tag = 'a'):
	profile = 0
	if version_tag is None:
		a_version_data = version_data.get('a')
		r_version_data = version_data.get('r', ())
		m_version_data = version_data.get('m', ())
		if a_version_data is None:
			version = None
		else:
			version = a_version_data['version']
			profile = 0
		if type(r_version_data) is dict and r_version_data != () and (version is None or VERSION_LIST.index(version) <= VERSION_LIST.index(r_version_data['version'])):
			version = r_version_data['version']
			profile = 1
		if type(m_version_data) is dict and m_version_data != () and (version is None or VERSION_LIST.index(version) <= VERSION_LIST.index(m_version_data['version'])):
			version = m_version_data['version']
			profile = 2
		ins_assert(version is not None)
	elif version_tag in version_data:
		version = version_data[version_tag]['version']
		profile = {'a': 0, 'r': 1, 'm': 2}.get(version_tag)
	else:
		ins_assert(False)

	if version_data['type'] == 'main':
		return (0, VERSION_LIST.index(version), profile)
	elif version_data['type'] == 'coproc':
		return (1, COPROC_VERSION_LIST.index(version), profile)
	else:
		ins_assert(False)

######## Set a fixed ordering for instructions: Java, ARM, 16-bit Thumb, 32-bit Thumb, A64 instructions are sorted, as well as coprocessor instructions

j32_order = {}

for ins in db['J32']:
	INS_LINE = ins['#']
	value = int(ins['code'].split(' ')[0], 16)
	try:
		value2 = int(ins['code'].split(' ')[1], 16)
	except IndexError:
		value2 = -1
	except ValueError:
		value2 = -1
	value = (value, value2)
	ins_assert(value not in j32_order)
	j32_order[value] = ins

a32_order = {}

for ins in db['A32']:
	INS_LINE = ins['#']
	rank = 0
	value = 0
	for i in range(32):
		c = ins['code'][31 - i]
		if c == '0':
			pass
		elif c == '1':
			value += 3 ** i
		else:
			for m in ins.get('match', ()):
				c = m[31 - i]
				if c == '0':
					break
				elif c == '1':
					value += 3 ** i
					break
			else:
				value += 2 * (3 ** i)
				rank += 1
	intro = get_proc_version(VERSION_TYPES[ins['added']], version_tag = None) if 'added' in ins else -1
	key = (value, rank, intro)
	if key in a32_order:
		print(f"Definition {ins['#']} overrides {a32_order[key]['#']}", file = sys.stderr)
		ins_assert(key not in a32_order)
	a32_order[key] = ins

cdp_order = {}
ldc_order = {}
mcr_order = {}
mrc_order = {}
mcrr_order = {}
mrrc_order = {}

cdp_coproc = set()
ldc_coproc = set()
mcr_coproc = set()
mrc_coproc = set()
mcrr_coproc = set()
mrrc_coproc = set()

for order, coproc, key in [
		(cdp_order,  cdp_coproc,  'CDP'),
		(ldc_order,  ldc_coproc,  'LDC'),
		(mcr_order,  mcr_coproc,  'MCR'),
		(mrc_order,  mrc_coproc,  'MRC'),
		(mcrr_order, mcrr_coproc, 'MCRR'),
		(mrrc_order, mrrc_coproc, 'MRRC'),
	]:
	for ins in db[key]:
		INS_LINE = ins['#']
		rank = 0
		value = 0
		for i in range(32):
			c = ins['code'][31 - i]
			if c == '0':
				pass
			elif c == '1':
				value += 3 ** i
			else:
				for m in ins.get('match', ()):
					c = m[31 - i]
					if c == '0':
						break
					elif c == '1':
						value += 3 ** i
						break
				else:
					value += 2 * (3 ** i)
					rank += 1

		version_data = VERSION_TYPES[ins['added']]
		intro = get_proc_version(version_data)
		key = (value, rank, intro)
		if key in order:
			print(f"Definition {ins['#']} overrides {order[key]['#']}", file = sys.stderr)
			ins_assert(key not in order)
		order[key] = ins

		mask = 0
		value = 0
		for i in range(4):
			c = ins['code'][23 - i]
			if c == '0':
				mask |= 1 << i
			elif c == '1':
				mask |= 1 << i
				value |= 1 << i

		for i in range(16):
			if (i & mask) == value:
				coproc.add(i)

a64_order = {}

for ins in db['A64']:
	INS_LINE = ins['#']
	rank = 0
	value = 0
	for i in range(32):
		c = ins['code'][31 - i]
		if c == '0':
			pass
		elif c == '1':
			value += 3 ** i
		else:
			for m in ins.get('match', ()):
				c = m[31 - i]
				if c == '0':
					break
				elif c == '1':
					value += 3 ** i
					break
			else:
				value += 2 * (3 ** i)
				rank += 1

	intro = get_proc_version(VERSION_TYPES[ins['added']], version_tag = None) if 'added' in ins else -1
	key = (value, rank, intro)
	if key in a64_order:
		print(f"Definition {ins['#']} overrides {a64_order[key]['#']}", file = sys.stderr)
		ins_assert(key not in a64_order)
	a64_order[key] = ins

t16_order = {}
t32_order = {}

for ins in db['T32']:
	INS_LINE = ins['#']
	rank1 = 0
	value1 = 0
	it = {None: 0, 'no': 1, 'end': 2, 'yes': 3}[ins.get('it')]
	e32 = {None: 0, 'no': 1, 'yes': 2}[ins.get('thumbee')]
	for i in range(16):
		c = ins['code'][15 - i]
		if c == '0':
			pass
		elif c == '1':
			value1 += 3 ** i
		else:
			for m in ins.get('match', ()):
				c = m[15 - i]
				if c == '0':
					break
				elif c == '1':
					value += 3 ** i
					break
			else:
				value1 += 2 * (3 ** i)
				rank1 += 1

	if 'added' not in ins:
		intro = -1
	else:
		intro = get_proc_version(VERSION_TYPES[ins['added']], version_tag = None)

	if ' ' in ins['code']:
		rank2 = 0
		value2 = 0
		for i in range(16):
			c = ins['code'][16 + 1 + 15 - i]
			if c == '0':
				pass
			elif c == '1':
				value2 += 3 ** i
			else:
				for m in ins.get('match', ()):
					c = m[16 + 1 + 15 - i]
					if c == '0':
						break
					elif c == '1':
						value2 += 3 ** i
						break
				else:
					value2 += 2 * (3 ** i)
					rank2 += 1
		key = (value1, rank1, value2, rank2, e32, it, intro)
		if key in t32_order:
			print(f"Definition {ins['#']} overrides {t32_order[key]['#']}", file = sys.stderr)
			ins_assert(key not in t32_order)
		t32_order[key] = ins
	else:
		key = (value1, rank1, e32, it, intro)
		if key in t16_order:
			print(f"Definition {ins['#']} overrides {t16_order[key]['#']}", file = sys.stderr)
			ins_assert(key not in t16_order)
		t16_order[key] = ins

def get_proc_version_range(ins):
	version_data = VERSION_TYPES[ins['added']]
	is_coproc, added, profile = get_proc_version(version_data, version_tag = None) # TODO: separate checks for M-profile architectures

	if is_coproc == 0:
		if 'removed' in ins and 'version' in VERSION_TYPES[ins['removed']]: # TODO: quick and dirty hack, make separate checks for M-profile architectures
			removed = VERSION_LIST.index(VERSION_TYPES[ins['removed']]['a']['version'])
		else:
			removed = len(VERSION_LIST) + 1
	else:
		if 'removed' in ins and VERSION_TYPES[ins['removed']]['type'] == 'coproc':
			removed = COPROC_VERSION_LIST.index(VERSION_TYPES[ins['removed']]['a']['version'])
		else:
			removed = COPROC_VERSION_LIST.index(COPROC_VERSION_END[VERSION_TYPES[ins['added']]['a']['version']]) + 1

	return is_coproc, (added, removed)

######## Sanity checks: should not have instruction encodings that collide

# sanity checks
for key, ins in a32_order.items():
	INS_LINE = ins['#']
	(value, rank, _) = key

	is_coproc, version_range = get_proc_version_range(ins)
	version_list = range(*version_range)

	for _version in version_list:
		_key = (value, rank, (is_coproc, _version))
		if key == _key:
			continue
		ins_assert(_key not in a32_order)

for order in [cdp_order, ldc_order, mcr_order, mrc_order, mcrr_order, mrrc_order]:
	for key, ins in order.items():
		INS_LINE = ins['#']
		(value, rank, _) = key

		is_coproc, version_range = get_proc_version_range(ins)
		version_list = range(*version_range)

		for _version in version_list:
			_key = (value, rank, (is_coproc, _version))
			if key == _key:
				continue
			ins_assert(_key not in order)

for key, ins in a64_order.items():
	INS_LINE = ins['#']
	(value, rank, _) = key

	is_coproc, version_range = get_proc_version_range(ins)
	version_list = range(*version_range)

	for _version in version_list:
		_key = (value, rank, _version)
		if key == _key:
			continue
		ins_assert(_key not in a64_order)

for key, ins in t16_order.items():
	INS_LINE = ins['#']
	(value1, rank1, e32, it, _) = key
	if e32 is None:
		e32_list = [False, True, None]
	else:
		e32_list = [None, e32]

	if it is None:
		it_list = [False, True, None]
	else:
		it_list = [None, it]

	version_list = range(VERSION_LIST.index(VERSION_TYPES[ins['added']]['a']['version']), VERSION_LIST.index(ins['removed'][0]) if 'removed' in ins else len(VERSION_LIST))

	for _e32 in e32_list:
		for _it in it_list:
			for _version in version_list:
				_key = (value1, rank1, _e32, _it, _version)
				if key == _key:
					continue
				ins_assert(_key not in t16_order)

for key, ins in t32_order.items():
	INS_LINE = ins['#']
	(value1, rank1, value2, rank2, e32, it, _) = key
	if e32 is None:
		e32_list = [False, True, None]
	else:
		e32_list = [None, e32]

	if it is None:
		it_list = [False, True, None]
	else:
		it_list = [None, it]

	# TODO: M-profile checks
	is_coproc, version_range = get_proc_version_range(ins)
	version_list = [(is_coproc, version) for version in range(*version_range)]

	for _e32 in e32_list:
		for _it in it_list:
			for _version in version_list:
				_key = (value1, rank1, value2, rank2, _e32, _it, (is_coproc, _version))
				if key == _key:
					continue
				ins_assert(_key not in t32_order)

######## Optional collision checks, between codes that do not collide but have overlapping encodings

if True:
	for mode, order, fpu in [
		('a32', a32_order, False),
		('t16', t16_order, False),
		('t32', t32_order, False),
		('a64', a64_order, False),

		('a32', cdp_order, True),
		('a32', ldc_order, True),
		('a32', mcr_order, True),
		('a32', mrc_order, True),
		('a32', mcrr_order, True),
		('a32', mrrc_order, True),
	]:
		tests = []
		for ins in order.values():
			_, mask1, value1, mask2, value2 = extract_mask(mode, ins['code']) #, variant = 'strong')
			tests.append((mask1, value1, mask2, value2, ins))

		for i in range(len(tests) - 1):
			for j in range(i + 1, len(tests)):
				mask1A, value1A, mask2A, value2A, insA = tests[i]
				mask1B, value1B, mask2B, value2B, insB = tests[j]

				if VERSION_TYPES[insA['added']]['type'] != VERSION_TYPES[insB['added']]['type']:
					continue

				if VERSION_TYPES[insA['added']]['type'] == 'coproc':
					if 'added' in insA and 'removed' in insB and COPROC_VERSION_LIST.index(VERSION_TYPES[insA['added']]['a']['version']) >= COPROC_VERSION_LIST.index(VERSION_TYPES[insB['removed']]['a']['version']):
						continue
					if 'added' in insB and 'removed' in insA and COPROC_VERSION_LIST.index(VERSION_TYPES[insB['added']]['a']['version']) >= COPROC_VERSION_LIST.index(VERSION_TYPES[insA['removed']]['a']['version']):
						continue
				else:
					if 'added' in insA and 'removed' in insB and VERSION_LIST.index(insA['added'][0]) >= VERSION_LIST.index(insB['removed'][0]):
						continue
					if 'added' in insB and 'removed' in insA and VERSION_LIST.index(insB['added'][0]) >= VERSION_LIST.index(insA['removed'][0]):
						continue

#				_, (addedA, removedA) = get_proc_version_range(insA)
#				_, (addedB, removedB) = get_proc_version_range(insB)
#				if 'added' in insA and 'removed' and addedA >= removedB:
#					continue
#				if 'added' in insB and 'removed' and addedB >= removedA:
#					continue

				if insA.get('thumbee') == 'no' and insB.get('thumbee') == 'yes':
					continue
				if insB.get('thumbee') == 'no' and insA.get('thumbee') == 'yes':
					continue

				mask1C = mask1A & mask1B
				mask2C = mask2A & mask2B
				if (value1A & mask1C) == (value1B & mask1C) and (mode != 't32' or (value2A & mask2C) == (value2B & mask2C)):
					collide = True
					for ex in insA.get('exclude', ()):
						_, x_mask1, x_value1, x_mask2, x_value2 = extract_mask(mode, ex)
						if (value1B & x_mask1) == (x_value1 & mask1B) or mode == 't32' and (value2B & x_mask2) == (x_value2 & mask2B):
							collide = False
							break
					if not collide:
						continue

					for ex in insB.get('exclude', ()):
						_, x_mask1, x_value1, x_mask2, x_value2 = extract_mask(mode, ex)
						if (value1A & x_mask1) == (x_value1 & mask1A) or mode == 't32' and (value2A & x_mask2) == (x_value2 & mask2A):
							collide = False
							break
					if not collide:
						continue
					print(f"{mode} collision: {insA['#']} and {insB['#']} {mask1A | mask1B:08X}")

######## Generating the disassembler and emulator cores

def get_value_size(mode, fields, initial_shift = 0):
	if mode == 'j32':
		return fields
	bit_offset = initial_shift
	for b1, b2, op in sorted(fields, key = lambda triple: (-triple[2], triple[0])):
		bit_offset += b2 - b1 + 1
	return bit_offset

def get_value(mode, name, fields, initial_shift = 0):
	if mode == 'j32':
		return f"_{name}"

	if mode in COPROC_ISAS:
		mode = 'a32'

	OPCODE_NIBBLE_COUNT = {'a32': 32, 't16': 16, 't32': 16, 'a64': 32}[mode] // 4
	if mode == 'a32' or mode == 'a64':
		opcode_varname = 'opcode'
	elif mode == 't16':
		opcode_varname = 'opcode1'
	result_fields = []
	bit_offset = initial_shift
	for b1, b2, op in fields:
		if mode == 't32':
			if op == 0:
				opcode_varname = 'opcode1'
			elif op == 1:
				opcode_varname = 'opcode2'
		m = (1 << (b2 + 1)) - (1 << b1)
		field = f"{opcode_varname} & 0x{m:0{OPCODE_NIBBLE_COUNT}X}"
		if b1 != bit_offset:
			disp = bit_offset - b1
			if disp > 0:
				field = f"({field}) << {disp}"
			else:
				field = f"({field}) >> {-disp}"
		result_fields.append(field)
		bit_offset += b2 - b1 + 1

	if len(result_fields) == 1:
		return result_fields[0]
	else:
		return ' | '.join('(' + field + ')' for field in result_fields)

def get_value_test(mode, fields, initial_shift = 0):
	if mode in COPROC_ISAS:
		mode = 'a32'

	OPCODE_NIBBLE_COUNT = {'a32': 32, 't16': 16, 't32': 16, 'a64': 32}[mode] // 4
	if mode == 'a32' or mode == 'a64':
		opcode_varname = 'opcode'
	elif mode == 't16' or mode == 't32':
		opcode_varname = 'opcode1'
	mask1 = 0
	mask2 = 0
	for b1, b2, op in fields:
		m = (1 << (b2 + 1)) - (1 << b1)
		if op == 0:
			mask1 |= m
		elif op == 1:
			ins_assert(mode == 't32')
			mask2 |= m
	test = []
	if mask1 != 0:
		test.append(f"{opcode_varname} & 0x{mask1:0{OPCODE_NIBBLE_COUNT}X}")
	if mask2 != 0:
		test.append(f"opcode2 & 0x{mask2:0{OPCODE_NIBBLE_COUNT}X}")
	if len(test) == 0:
		return 0, 0
	elif len(test) == 1:
		return test[0], mask1
	else:
		return '(' + test[0] + ') != 0 || (' + test[1] + ') != 0', (mask1, mask2)

def parse_variable(mode, pattern, varfields, test_only = False, mask_only = False):
	initial_shift = 0
	negate = False
	if "'" in pattern:
		varname = None # TODO
		fields = []
		for part in reversed(pattern.split("'")):
			if part.isdigit():
				# TODO: only 0s are supported
				ins_assert(len(fields) == 0 and initial_shift == 0)
				initial_shift = len(part)
			else:
				fields += varfields[part]
	else:
		varname = pattern
		if varname.startswith('~'):
			negate = True
			varname = varname[1:]
		ins_assert(varname in varfields, f"Undefined {varname}")
		fields = varfields[varname]

	if mask_only:
		return get_value_test(mode, fields)
	elif test_only:
		value, _ = get_value_test(mode, fields)
	else:
		value = get_value(mode, varname, fields, initial_shift = initial_shift)
	size = get_value_size(mode, fields, initial_shift = initial_shift)
	if negate:
		value = f'({value}) ^ 0x{(1 << size) - 1:X}'
	return value, size

def parse_expression(mode, pattern, varfields, test_only = False, parentheses = False):
	if mode in COPROC_ISAS:
		mode = 'a32'

	if '&' in pattern or '+' in pattern or '-' in pattern or '*' in pattern or '<<' in pattern or '>>' in pattern:
		# this order approximates a precedence table
		if '+' in pattern:
			operator = '+'
		elif '-' in pattern:
			operator = '-'
		elif '*' in pattern:
			operator = '*'
		elif '&' in pattern:
			operator = '&'
		elif '>>' in pattern:
			operator = '>>'
		elif '<<' in pattern:
			operator = '<<'
		ins_assert(not test_only)
		values = []
		for part in pattern.split(operator):
			if part == '':
				values.append('')
			elif part.isdigit():
				values.append(part)
			else:
				values.append(parse_expression(mode, part, varfields, parentheses = True))
		if operator == '-' and values[0] == '':
			values.pop(0)
			values[0] = '-' + values[0]
		return '(' + f' {operator} '.join(values) + ')'
	elif test_only:
		negate = False
		if pattern.startswith('!'):
			negate = True
			pattern = pattern[1:]
		value, size = parse_variable(mode, pattern, varfields, test_only = test_only)
		if parentheses or negate:
			value = '(' + value + ')'
		if negate:
			value = '!' + value
		return value
	else:
		if pattern.startswith('signextend '):
			pattern = pattern[11:]
			value, size = parse_variable(mode, pattern, varfields, test_only = test_only)
			return f'sign_extend({size}, {value})'
		else:
			value, size = parse_variable(mode, pattern, varfields, test_only = test_only)
			if parentheses:
				value = '(' + value + ')'
			return value

LINE_NUMBER = 0
def print_file(file, line, end = '\n'):
	global LINE_NUMBER
	line += end
	print(line, file = file, end = '')
	LINE_NUMBER += line.count('\n')

def replace_placeholders(text, placeholders):
	# search in reversed length order to make sure shorter sequences do not get replaced before longer sequences

	for key, value in sorted((pair for pair in placeholders.items() if pair[0].endswith('=')), key = lambda pair: -len(pair[0])):
		if key.endswith('[]='):
			mark = key[:-2]
		else:
			mark = key[:-1]

		pos = 0
		while True:
			pos = text.find(mark, pos)
			if pos == -1:
				break

			new_placeholders = {}
			if mark.endswith('['):
				pos2 = text.find(']', pos + len(mark))
				ins_assert(pos2 != -1)
				index = text[pos + len(mark):pos2]
				pos2 += 1
				new_placeholders['$?'] = index
			else:
				pos2 = pos + len(mark)

			if not text[pos2:].lstrip().startswith('='):
				pos += 1
				continue
			pos2 = text.find('=', pos2)
			ins_assert(pos2 != -1)
			if len(text) >= pos2 + 2 and text[pos2 + 1] == '=':
				pos += 1
				continue
			pos3 = text.find(';', pos2)
			ins_assert(pos3 != -1)
			source_value = text[pos2 + 1:pos3].strip()
			new_placeholders['$$'] = source_value

			pos2 = pos3
			replacement = replace_placeholders(value, new_placeholders)
			text = text[:pos] + replacement + text[pos2:]

			pos = 0

	for key, value in sorted((pair for pair in placeholders.items() if not pair[0].endswith('=')), key = lambda pair: -len(pair[0])):
		if key.endswith('[]'):
			mark = key[:-1]
		else:
			mark = key

		while True:
			pos = text.find(mark)
			if pos == -1:
				break
			if mark.endswith('['):
				pos2 = text.find(']', pos + len(mark))
				ins_assert(pos2 != -1)
				index = text[pos + len(mark):pos2]
				pos2 += 1
				replacement = replace_placeholders(value, {'$?': index})
			else:
				pos2 = pos + len(mark)
				replacement = value
			text = text[:pos] + replacement + text[pos2:]
	return text

# creates a logical clause to check version and features
def generate_predicate(version, features, cpu = 'cpu', is_coproc = False):
	predicates = []
	if version is not None and version != -1:
		if not is_coproc:
			predicates.append(f'{cpu}->config.version >= {VERSION_NAMES[VERSION_LIST[version]]}')
		else:
			if COPROC_VERSION_LIST[version] == 'FPA':
				predicates.append(f'({cpu}->config.features & (1 << FEATURE_FPA))')
			else:
				predicates.append(f'{cpu}->config.fp_version >= {COPROC_VERSION_NAMES[COPROC_VERSION_LIST[version]]}')

	feature_list = []
	for feature in sorted(features):
		feature_name = FEATURE_LIST[feature]
		prefix = '!' if feature == 'G' else ''
		if type(feature_name) is not str:
			feature_member_list = []
			for feature_member in feature_name:
				feature_member_list.append(f'({cpu}->config.features & (1 << {feature_member}))')
			feature_list.append('(' + ' || '.join(feature_member_list) + ')')
		else:
			assert type(feature_name) is str
			feature_list.append(f'{prefix}({cpu}->config.features & (1 << {feature_name}))')

	if len(feature_list) == 1:
		predicates.append(feature_list[0])
	elif len(feature_list) > 1:
		predicates.append('(' + ' || '.join(feature_list) + ')')

	if len(predicates) == 0:
		return None
	else:
		return " && ".join(predicates)

TABLEN = 24 # j32 only

COUNTER = 0

def generate_branches(mode, order, indent, method, cpnum = None):
	global LINE_NUMBER, INS_LINE, COUNTER

	# a32 - ARM26, ARM32
	# t16 - 16-bit Thumb instruction
	# t32 - 32-bit Thumb instruction
	# a64 - ARM64
	# j32 - Java

	if method == 'parse':
		cpu = 'dis'
	elif method == 'step':
		cpu = 'cpu'
	else:
		assert False

	if cpnum is not None:
		assert mode in COPROC_ISAS
		assert method == 'step'

	else_kwd = ''
	for _, ins in sorted(order.items(), key = lambda pair: pair[0]):
		INS_LINE = ins['#']

		if mode == 'j32':
			ins_assert(ins.get('added') in {'JVM', '5TEJ', 'picoJava', 'extension'})

		if method == 'step' and 'begin' not in ins:
			# not implemented
			continue
		elif method == 'parse' and 'asm' not in ins and 'ual' not in ins:
			# no parse (usually a fallback is used)
			continue

		if cpnum is not None:
			# filter out coprocessor instructions for a different coprocessor
			_, mask, value, _, _ = extract_mask(mode, ins['code'])
			mask = (mask >> 8) & 0xF
			value = (value >> 8) & 0xF
			if (cpnum & mask) != value:
				continue

		if mode == 'j32':
			# To encode Java instructions, a different encoding syntax is used to the other instruction sets, as hexadecimal instead of binary
			make_wide = False
			if ' ' in ins['code']:
				pars = ins['code'].split(' ')
				code = int(pars.pop(0), 16)
				code2 = None
				if len(pars) > 0 and len(pars[0]) == 2:
					try:
						code2 = int(pars[0], 16)
						pars.pop(0)
						code = (code << 8) | code2
					except ValueError:
						pass
			else:
				pars = []
				code = int(ins['code'], 16)
			# prepare variables later
			varfields = {}
		else:
			# varfields contains a series of bitfields, (start, end, opcode word number)
			varfields, mask1, value1, mask2, value2 = extract_mask(mode, ins['code'])

		if mode == 'a32' or mode == 'a64' or mode in COPROC_ISAS:
			# 32-bit wide instructions

			if mode == 'a32' and ins.get('a26') == 'yes':
				is_arm26 = 'is_arm26' if method == 'parse' else 'a32_is_arm26(cpu)'
				a26_condition = f"{is_arm26} && "
			else:
				a26_condition = ""
			for m in ins.get('match', ()):
				_, m_mask1, m_value1, _, _ = extract_mask(mode, m)
				ins_assert((value1 & m_mask1) == (m_value1 & mask1))
				mask1 |= m_mask1
				value1 |= m_value1
			condition = f"{a26_condition}(opcode & 0x{mask1:08X}) == 0x{value1:08X}"
			for ex in ins.get('exclude', ()):
				if '\texcept\t' in ex:
					ex, tag = ex.split('\texcept\t')
					_, mask1, value1, _, _ = extract_mask(mode, ex)
					feature = FEATURE_LIST[FEATURES[tag]]

					assert type(feature) is str
					predicate = f"({cpu}->config.features & (1 << {feature}))"

					condition = f"{condition} && ((opcode & 0x{mask1:08X}) != 0x{value1:08X} || {predicate})"

				elif '\twhen\t' in ex:
					ex, ver = ex.split('\twhen\t')
					features = VERSION_TYPES[ver]['a']['feature']
					assert len(features) == 1
					feature = FEATURE_LIST[features[0]]
					_, mask1, value1, _, _ = extract_mask(mode, ex)
					assert type(feature) is str
					condition = f"{condition} && ((opcode & 0x{mask1:08X}) != 0x{value1:08X} || !({cpu}->config.features & (1 << {feature})))"

				else:
					_, mask1, value1, _, _ = extract_mask(mode, ex)
					condition = f"{condition} && (opcode & 0x{mask1:08X}) != 0x{value1:08X}"

		elif mode == 't16' or mode == 't32':
			# Thumb instruction set

			if method == 'parse':
				is_thumbee = 'is_thumbee'
				if 'it' not in ins:
					it_condition = ""
				elif ins['it'] == 'yes':
					it_condition = " && dis->t32.it_block_count > 0"
				elif ins['it'] == 'no':
					it_condition = " && dis->t32.it_block_count == 0"
				elif ins['it'] == 'end':
					it_condition = " && dis->t32.it_block_count <= 1"
				else:
					ins_assert(False)
			elif method == 'step':
				is_thumbee = 't32_is_thumbee(cpu)'
				if 'it' not in ins:
					it_condition = ""
				elif ins['it'] == 'yes':
					it_condition = " && t32_in_it_block(cpu)"
				elif ins['it'] == 'no':
					it_condition = " && !t32_in_it_block(cpu)"
				elif ins['it'] == 'end':
					it_condition = " && (!t32_in_it_block(cpu) || t32_last_in_it_block(cpu))"
				else:
					ins_assert(False)

			ee_condition = f"{'' if ins['thumbee'] == 'yes' else '!'}{is_thumbee} && " if 'thumbee' in ins else ""

			if mode == 't16':
				for m in ins.get('match', ()):
					_, m_mask1, m_value1, _, _ = extract_mask(mode, m)
					ins_assert((value1 & m_mask1) == (m_value1 & mask1))
					mask1 |= m_mask1
					value1 |= m_value1

				condition = f"{ee_condition}(opcode1 & 0x{mask1:04X}) == 0x{value1:04X}{it_condition}"
				for ex in ins.get('exclude', ()):
					_, mask1, value1, _, _ = extract_mask(mode, ex)
					condition = f"{condition} && (opcode1 & 0x{mask1:04X}) != 0x{value1:04X}"
			elif mode == 't32':
				for m in ins.get('match', ()):
					_, m_mask1, m_value1, m_mask2, m_value2 = extract_mask(mode, m)
					ins_assert((value1 & m_mask1) == (m_value1 & mask1))
					ins_assert((value2 & m_mask2) == (m_value2 & mask2))
					mask1 |= m_mask1
					value1 |= m_value1
					mask2 |= m_mask2
					value2 |= m_value2

				condition = f"{ee_condition}(opcode1 & 0x{mask1:04X}) == 0x{value1:04X} && (opcode2 & 0x{mask2:04X}) == 0x{value2:04X}{it_condition}"
				for ex in ins.get('exclude', ()):
					_, mask1, value1, mask2, value2 = extract_mask(mode, ex)
					if mask2 == 0:
						assert mask1 != 0
						condition = f"{condition} && (opcode1 & 0x{mask1:04X}) != 0x{value1:04X}"
					elif mask1 == 0:
						condition = f"{condition} && (opcode2 & 0x{mask2:04X}) != 0x{value2:04X}"
					else:
						condition = f"{condition} && !((opcode1 & 0x{mask1:04X}) == 0x{value1:04X} && (opcode2 & 0x{mask2:04X}) == 0x{value2:04X})"
			else:
				ins_assert(False)

		elif mode == 'j32':
			condition = f"opcode == 0x{code:02X}"

			if method == 'parse':
				fetch8 = 'file_fetch8'
				fetch16be = 'file_fetch16be'
				fetch32be = 'file_fetch32be'
			elif method == 'step':
				fetch8 = 'j32_fetch8'
				fetch16be = 'j32_fetch16'
				fetch32be = 'j32_fetch32'
		else:
			ins_assert(False)

		# Handle versioning and feature control

		if 'added' not in ins or mode == 'j32':
			added = None
		else:
			added = VERSION_TYPES[ins['added']]

		if 'removed' not in ins or mode == 'j32':
			removed = None
		else:
			removed = VERSION_TYPES[ins['removed']]

		if mode == 'j32':
			# Java bytecode has different rules for introduction

			if method == 'parse':
				if ins['added'] == 'JVM':
					pass
				elif ins['added'] == '5TEJ':
					condition = f'{cpu}->config.jazelle_implementation == ARM_JAVA_JAZELLE && ' + condition
				elif ins['added'] == 'picoJava':
					condition = f'{cpu}->config.jazelle_implementation >= ARM_JAVA_PICOJAVA && ' + condition
				elif ins['added'] == 'extension':
					condition = f'{cpu}->config.jazelle_implementation >= ARM_JAVA_EXTENSION && ' + condition
			elif method == 'step':
				if 'usedby' not in ins:
					pass # not used
				elif ins['usedby'] == 'JVM':
					condition = f'{cpu}->config.jazelle_implementation >= ARM_JAVA_EXTENSION && ' + condition
				elif ins['usedby'] == '5TEJ':
					condition = f'{cpu}->config.jazelle_implementation >= ARM_JAVA_JAZELLE && ' + condition
				elif ins['usedby'] == 'picoJava' or ins['added'] == 'extension':
					condition = f'{cpu}->config.jazelle_implementation >= ARM_JAVA_EXTENSION && ' + condition

		elif added is None or added['type'] == 'main':
			# non-coprocessor instructions

			if added is not None:
				intro = VERSION_LIST.index(added['a']['version']) if 'a' in added else None
				feature_enable = set(added['a']['feature']) if 'a' in added else set()

				if 'r' not in added:
					r_intro = None
					r_feature_enable = set()
				elif added['r'] is None:
					r_intro = False
					r_feature_enable = set()
				else:
					r_intro = VERSION_LIST.index(added['r']['version'])
					r_feature_enable = set(added['r']['feature'])

				if 'm' not in added:
					m_intro = None
					m_feature_enable = set()
				elif added['m'] is None:
					m_intro = False
					m_feature_enable = set()
				else:
					m_intro = VERSION_LIST.index(added['m']['version'])
					m_feature_enable = set(added['m']['feature'])
			else:
				ins_assert(mode == 'j32')
				intro = -1
				r_intro = None
				m_intro = None
				feature_enable = set()
				r_feature_enable = set()
				m_feature_enable = set()

			if removed is not None:
				assert removed['type'] == 'main'
				last = VERSION_LIST.index(removed['a']['version']) if 'a' in removed else None
				feature_disable = set(removed['a']['feature']) if 'a' in removed else set()

				if 'r' not in removed:
					r_last = None
					r_feature_disable = set()
				elif removed['r'] is None:
					r_last = False
					r_feature_disable = set()
				else:
					r_last = VERSION_LIST.index(removed['r']['version'])
					r_feature_disable = set(removed['r']['feature'])

				if 'm' not in removed:
					m_last = None
					m_feature_disable = set()
				elif removed['m'] is None:
					m_last = False
					m_feature_disable = set()
				else:
					m_last = VERSION_LIST.index(removed['m']['version'])
					m_feature_disable = set(removed['m']['feature'])
			else:
				last = None
				r_last = None
				m_last = None
				feature_disable = set()
				r_feature_disable = set()
				m_feature_disable = set()

			if mode == 'a32':
				if intro == VERSION_LIST.index('1'):
					intro = -1
				if last == VERSION_LIST.index('8'):
					last = None

			elif mode == 't16' or mode == 't32':
				if 'T' in feature_enable:
					feature_enable.remove('T')
				assert 'T' not in feature_disable

				if ins.get('thumbee') == 'yes':
					if intro == VERSION_LIST.index('7'):
						intro = -1
					if last == VERSION_LIST.index('7'):
						last = None

				elif mode == 't32':
					if 'T2' in feature_enable and m_intro is False:
						# if this is not valid for M-profile, we do not need to test for Thumb2 (only ARMv6-M is M-profile but not Thumb2)
						feature_enable.remove('T2')

					if intro == VERSION_LIST.index('6'):
						intro = -1
					if last == VERSION_LIST.index('8'):
						last = None

				else:
					if intro == VERSION_LIST.index('4'):
						intro = -1
					if last == VERSION_LIST.index('8'):
						last = None

			elif mode == 'a64':
				if intro == VERSION_LIST.index('8'):
					intro = -1
				if last == VERSION_LIST.index('8'):
					last = None

			r_profile_check = f'(({cpu}->config.features & FEATURE_PROFILE_MASK) == ARM_PROFILE_R)'
			m_profile_check = f'(({cpu}->config.features & FEATURE_PROFILE_MASK) == ARM_PROFILE_M)'

			predicates1 = generate_predicate(intro, feature_enable,  cpu = cpu)
			predicates2 = generate_predicate(last,  feature_disable, cpu = cpu)

			if predicates2 is not None:
				if predicates2.startswith('!'):
					predicates2 = predicates2[1:]
				else:
					predicates2 = '!(' + predicates2 + ')'

				if predicates1 is None:
					predicates = predicates2
				else:
					predicates = predicates1 + ' && ' + predicates2
			else:
				predicates = predicates1

			if r_intro is not None and r_intro is not False:
				r_predicates1 = generate_predicate(r_intro, r_feature_enable,  cpu = cpu)
				r_predicates2 = generate_predicate(r_last,  r_feature_disable, cpu = cpu)

				if r_predicates2 is not None:
					if r_predicates2.startswith('!'):
						r_predicates2 = r_predicates2[1:]
					else:
						r_predicates2 = '!(' + r_predicates2 + ')'

					if r_predicates1 is None:
						r_predicates = r_predicates2
					else:
						r_predicates = r_predicates1 + ' && ' + r_predicates2
				else:
					r_predicates = r_predicates1
			else:
				r_predicates = None

			if m_intro is not None and m_intro is not False:
				m_predicates1 = generate_predicate(m_intro, m_feature_enable,  cpu = cpu)
				m_predicates2 = generate_predicate(m_last,  m_feature_disable, cpu = cpu)

				if m_predicates2 is not None:
					if m_predicates2.startswith('!'):
						m_predicates2 = m_predicates2[1:]
					else:
						m_predicates2 = '!(' + m_predicates2 + ')'

					if m_predicates1 is None:
						m_predicates = m_predicates2
					else:
						m_predicates = m_predicates1 + ' && ' + m_predicates2
				else:
					m_predicates = m_predicates1
			else:
				m_predicates = None

			if r_predicates is not None:
				if predicates is not None:
					predicates = f'({r_profile_check} ? {r_predicates} : {predicates})'
				else:
					predicates = f'(!{r_profile_check} || {r_predicates})'
			else:
				if predicates is not None:
					predicates = predicates
				if r_intro is False:
					predicates = f'!{r_profile_check}'

			if m_predicates is not None:
				if predicates is not None:
					condition = f'({m_profile_check} ? {m_predicates} : {predicates}) && ' + condition
				else:
					condition = f'(!{m_profile_check} || {m_predicates}) && ' + condition
			else:
				if predicates is not None:
					condition = predicates + ' && ' + condition
				if m_intro is False:
					condition = f'!{m_profile_check} && ' + condition

		elif added['type'] == 'coproc':
			# coprocessor instructions

			intro_name = added.get('actual_version', added['a']['version'])
			intro = COPROC_VERSION_LIST.index(intro_name)
			feature_enable = set(added['a']['feature'])

			if removed is not None:
				last_name = removed.get('actual_version', removed['a']['version'])
				if removed['type'] == 'coproc':
					last = COPROC_VERSION_LIST.index(last_name)
				else:
					last = VERSION_LIST.index(last_name)
				feature_disable = set(removed['a']['feature'])
			else:
				if COPROC_VERSION_END[intro_name] == COPROC_VERSION_LIST[-1]:
					last = None
				else:
					last = COPROC_VERSION_LIST.index(COPROC_VERSION_END[intro_name])
				feature_disable = set()

			predicates1 = generate_predicate(intro, feature_enable,  cpu = cpu, is_coproc = True)
			predicates2 = generate_predicate(last,  feature_disable, cpu = cpu, is_coproc = removed is None or removed['type'] == 'coproc')

			if predicates2 is not None:
				if predicates2.startswith('!'):
					predicates2 = predicates2[1:]
				else:
					predicates2 = '!(' + predicates2 + ')'

				if predicates1 is None:
					predicates = predicates2
				else:
					predicates = predicates1 + ' && ' + predicates2
			else:
				predicates = predicates1

			if predicates is not None:
				condition = predicates + ' && ' + condition

		print_file(file, indent + f"{else_kwd}if({condition})")
		print_file(file, indent + "{")

		if mode == 'j32':
			# Java code can use the wide prefix
			uses_wide = False
			for par in pars:
				name, fmt = par.split(':')
				if fmt == 'iW':
					uses_wide = True
					break

			if uses_wide:
				options = [True, False]
			else:
				options = [False]
		else:
			# Support for classic and unified assembly syntaxes
			if method == 'parse':
				options = []
				if 'asm' in ins:
					options.append('asm')
				if 'ual' in ins:
					options.append('ual')
				ins_assert(len(options) != 0)
			else:
				options = [None]

		if len(options) > 1:
			indent1 = indent + '\t'
		else:
			indent1 = indent

		# Generate the actual code

		for option_index, option in enumerate(options):
			if len(options) > 1:
				if option_index == 0:
					if mode == 'j32':
						if method == 'parse':
							j32_wide_name = 'dis->j32.wide'
						elif method == 'step':
							j32_wide_name = 'j32_wide'
						print_file(file, indent + f"\tif({j32_wide_name})")
					else:
						print_file(file, indent + "\tif(dis->syntax == SYNTAX_DIVIDED)")
					print_file(file, indent + "\t{")
				else:
					print_file(file, indent + "\t}")
					print_file(file, indent + "\telse")
					print_file(file, indent + "\t{")

			if method == 'parse':
				hadargs = False
				fmtstr = ''
				args = []

			if mode == 'j32':
				# Since Java encoding is modified by the 'wide' prefix, handle immediates here
				tablen = TABLEN - 4
				wide = option

				for par in pars:
					name, fmt = par.split(':')
					if fmt == 'i8' or (not wide and fmt == 'iW'):
						varfields[name] = 8
						print_file(file, indent1 + f"\tuint8_t _{name} = {fetch8}({cpu});")
						if method == 'parse':
							fmtstr += " %02X"
							args.append(f", _{name} & 0xFF")
							tablen -= 3
					elif fmt == 'i16' or (wide and fmt == 'iW'):
						varfields[name] = 16
						print_file(file, indent1 + f"\tuint16_t _{name} = {fetch16be}({cpu});")
						if method == 'parse':
							fmtstr += " %02X %02X"
							args.append(f", (_{name} >> 8) & 0xFF")
							args.append(f", _{name} & 0xFF")
							tablen -= 6
					elif fmt == 'i32':
						varfields[name] = 32
						print_file(file, indent1 + f"\tuint32_t _{name} = {fetch32be}({cpu});")
						if method == 'parse':
							fmtstr += " %02X %02X %02X %02X"
							args.append(f", (_{name} >> 24) & 0xFF")
							args.append(f", (_{name} >> 16) & 0xFF")
							args.append(f", (_{name} >> 8) & 0xFF")
							args.append(f", _{name} & 0xFF")
							tablen -= 12
					elif fmt == 'padding':
						if method == 'parse':
							if fmtstr != "":
								print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
							fmtstr = ''
							args = []
							print_file(file, indent1 + "\tuint8_t buffer[3];")
							print_file(file, indent1 + "\tsize_t buffer_size = file_fetch_align32(dis, (void *)buffer);")
							print_file(file, indent1 + "\tfor(size_t i = 0; i < buffer_size; i++)")
							print_file(file, indent1 + '\t\tprintf(" %02X", buffer[i]);')
						elif method == 'step':
							print_file(file, indent1 + "\tfetch_align32(cpu);")
						else:
							ins_assert(False)
					elif fmt.startswith('label['):
						if method == 'parse':
							print_file(file, indent1 + "\tdis->j32.parse_state = J32_PARSE_LINE;")
							count = fmt[6:-1]
							value = parse_expression('j32', count, varfields)
							print_file(file, indent1 + f"\tdis->j32.parse_state_count = {value};")
					elif fmt.startswith('pair['):
						if method == 'parse':
							print_file(file, indent1 + "\tdis->j32.parse_state = J32_PARSE_PAIR;")
							count = fmt[5:-1]
							value = parse_expression('j32', count, varfields)
							print_file(file, indent1 + f"\tdis->j32.parse_state_count = {value};")
					else:
						ins_assert(False)

				if method == 'parse':
					if tablen <= 0:
						fmtstr += '>\\n' + ('\\t' * (2 + TABLEN // 8))
					else:
						fmtstr += '>' + ('\\t' * ((tablen + 7) // 8))

			if method == 'parse':
				# Generate code for the parser

				if mode == 'j32':
					syntax = 'asm'
				else:
					syntax = option

				if 'coproc' in ins:
					ins_assert(mode in {'a32', 't32'})
					if mode == 'a32':
						opcode_name = 'opcode'
						condition_name = '(opcode & 0xF0000000) >> 28'
					elif mode == 't32':
						opcode_name = '((uint32_t)opcode1 << 16) | opcode2'
						condition_name = 'it_get_condition(dis)'
					print_file(file, indent1 + f'\tif(!{ins["coproc"]}_parse(dis, {opcode_name}, {condition_name}))')
					print_file(file, indent1 + '\t{')
					indent1 += '\t'

				# Process format string
				i = 0
				while i < len(ins[syntax]):
					c = ins[syntax][i]
					i += 1
					if c == ' ' and not hadargs and mode != 'j32':
						# First space character is converted to a tab
						fmtstr += '\\t'
						hadargs = True
					elif c in {'{', '}'} and i < len(ins[syntax]) and ins[syntax][i] == c:
						# Double parentheses are converted to single parentheses
						fmtstr += c
						i += 1
					elif c == '{':
						# Syntax interpolation
						j = ins[syntax].find('}', i)
						ins_assert(j != -1)
						p = ins[syntax][i:j]
						if '(' in p:
							# Function call
							fun = p[:p.find('(')]
							arg = p[p.find('(') + 1:-1]
							if fun == 'cond':
								if mode == 'a32':
									if arg != '':
										fmtstr += '%s'
										value = f'a32_condition[(opcode & 0xF0000000) >> 28]'
										args.append(f", {value}")
								elif mode == 't16' or mode == 't32':
									fmtstr += '%s'
									varname = p[5:-1]
									if varname == '':
										args.append(", a32_condition[it_get_condition(dis)]")
									else:
										ins_assert(varname in varfields)
										value = get_value(mode, varname, varfields[varname])
										args.append(f", a32_condition[{value}]")
								elif mode in COPROC_ISAS:
									fmtstr += '%s'
									args.append(", a32_condition[current_condition]")
								elif mode == 'a64':
									fmtstr += '%s'
									value = f'a64_condition[(opcode & 0xF0000000) >> 28]'
									args.append(f", {value}")
								else:
									ins_assert(False, f"Undefined function: {fun}")
							elif fun == 'cond_or_s':
								ins_assert(mode == 't16' or mode == 't32')
								fmtstr += '%s'
								args.append(', dis->t32.it_block_count > 0 ? a32_condition[it_get_condition(dis)] : dis->syntax == SYNTAX_UNIFIED ? "s" : ""')
							elif fun == 'operand':
								if mode == 'a32':
									print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
									fmtstr = ''
									args = []
									print_file(file, indent1 + "\tif((opcode & 0x02000000) != 0)")
									print_file(file, indent1 + "\t{")
									print_file(file, indent1 + '\t\tprintf("#0x%08X", a32_get_immediate_operand(opcode));')
									print_file(file, indent1 + "\t}")
									print_file(file, indent1 + "\telse")
									print_file(file, indent1 + "\t{")
									print_file(file, indent1 + '\t\tprintf("r%d", (opcode & 0xF));')
									print_file(file, indent1 + "\t\ta32_print_operand_shift(opcode);")
									print_file(file, indent1 + "\t}")
								else:
									ins_assert(False)
							elif fun == 'adr_operand':
								if mode == 'a32':
									print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
									fmtstr = ''
									args = []
									print_file(file, indent1 + "\tif((opcode & 0x02000000) == 0)")
									print_file(file, indent1 + "\t{")
									print_file(file, indent1 + '\t\tprintf("#%s0x%08X", (opcode & 0x00800000) ? "" : "-", (opcode & 0xFFF));')
									print_file(file, indent1 + "\t}")
									print_file(file, indent1 + "\telse")
									print_file(file, indent1 + "\t{")
									print_file(file, indent1 + '\t\tprintf("%sr%d", (opcode & 0x00800000) ? "" : "-", (opcode & 0xF));')
									print_file(file, indent1 + "\t\ta32_print_operand_shift(opcode);")
									print_file(file, indent1 + "\t}")
								else:
									ins_assert(False)
							elif fun == 'simd_operand_type':
								assert mode == 'a32' or mode == 't32'
								fmtstr += '%s'
								if mode == 'a32':
									args.append(', simd_operand_type[((opcode >> 8) & 0xF) | ((opcode >> 1) & 0x10)]')
								elif mode == 't32':
									args.append(', simd_operand_type[((opcode2 >> 8) & 0xF) | ((opcode2 >> 1) & 0x10)]')
							elif fun == 'simd_operand':
								ins_assert(mode == 'a32' or mode == 't32' or mode == 'a64')
								fmtstr += '0x%016"PRIX64"'
								if mode == 'a32':
									args.append(', a32_get_simd_operand(NULL, opcode)')
								elif mode == 't32':
									args.append(', t32_get_simd_operand(NULL, opcode1, opcode2)')
								elif mode == 'a64':
									args.append(', a64_get_simd_operand(NULL, opcode)')
							elif fun == 'immed':
								assert mode == 'a32' or mode == 't32'
								fmtstr += '0x%08X'
								if mode == 'a32':
									args.append(", a32_get_immediate_operand(opcode)")
								elif mode == 't32':
									args.append(", t32_get_immediate_operand(opcode1, opcode2)")
								else:
									ins_assert(False)
							elif fun == 'shift':
								if mode == 't32':
									print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
									fmtstr = ''
									args = []
									print_file(file, indent1 + "\tt32_print_operand_shift(opcode2);")
								else:
									ins_assert(False)
							elif fun == 'store_t16':
								ins_assert(mode == 't16')
								print_file(file, indent1 + "\tdis->t32.jump_instruction = opcode1;")
							elif fun == 't32label':
								assert mode == 't16' or mode == 't32'
								if mode == 't16':
									print_file(file, indent1 + "\tuint16_t opcode2 = opcode1;")
									print_file(file, indent1 + "\topcode1 = dis->t32.jump_instruction;")
								fmtstr += '0x%08X'
								fields = [(0, 10, 1), (0, 9, 0), (11, 11, 1), (13, 13, 1), (10, 10, 0)]
								initial_shift = 1
								value = get_value('t32', None, fields, initial_shift = initial_shift)
								size = get_value_size('t32', fields, initial_shift = initial_shift)
								args.append(f', (uint32_t)dis->pc + sign_extend({size}, ({value}) ^ ((uint32_t)(~opcode2 & 0x0400) << 12) ^ ((uint32_t)(~opcode2 & 0x0400) << 13))')
								#print_file(file, f"printf(\"%08X\\n\", ({value}) ^ ((uint32_t)(~opcode2 & 0x0400) << 12) ^ ((uint32_t)(~opcode2 & 0x0400) << 13));")
							elif fun == 'reglist':
								ins_assert(mode == 'a32' or mode == 't16' or mode == 't32')
								value = parse_expression(mode, arg, varfields)
								print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
								fmtstr = ''
								args = []
								print_file(file, indent1 + f"\ta32_print_register_list({value});")
							elif fun == 'sreglist' or fun == 'dreglist':
								ins_assert(mode == 'ldc')
								num, count = arg.split(',')
								num = parse_expression(mode, num, varfields)
								count = parse_expression(mode, count, varfields)
								print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
								fmtstr = ''
								args = []
								print_file(file, indent1 + f"\ta32_print_fpregister_list({num}, {count}, {'true' if fun[0] == 'd' else 'false'});")
							elif fun == 'simd_postindex':
								print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
								fmtstr = ''
								args = []
								value = parse_expression(mode, arg, varfields)
								print_file(file, indent1 + f"switch({value})")
								print_file(file, indent1 + "{")
								print_file(file, indent1 + "case 13:")
								print_file(file, indent1 + "\tprintf(\"!\");")
								print_file(file, indent1 + "\tbreak;")
								print_file(file, indent1 + "case 15:")
								print_file(file, indent1 + "\tbreak;")
								print_file(file, indent1 + "default:")
								print_file(file, indent1 + f"\tprintf(\", r%d\", {value});")
								print_file(file, indent1 + "\tbreak;")
								print_file(file, indent1 + "}")
							elif fun == 'extend':
								ins_assert(mode == 'a64')
								if fmtstr != "":
									print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
								arg = arg.split(',')
								op = parse_expression(mode, arg[0], varfields)
								am = parse_expression(mode, arg[1], varfields)
								if arg[2] == '0':
									xr = 'false'
								elif arg[2] == '1':
									xr = 'true'
								else:
									xr = parse_expression(mode, arg[2], varfields, test_only = True)
								fmtstr = ''
								args = []
								print_file(file, indent1 + f"\ta64_print_extension({op}, {am}, {xr});")
							elif fun == 'condlist':
								# only for the IT instruction
								ins_assert(mode == 't16')
								print_file(file, indent1 + "\tparser_set_it_condition(dis, opcode1);")
								fmtstr += "%s%s%s"
								args.append(', dis->t32.it_block_count < 3 ? "" : ((dis->t32.it_block_mask >> 2) ^ (dis->t32.it_block_condition)) & 1 ? "e" : "t"')
								args.append(', dis->t32.it_block_count < 4 ? "" : ((dis->t32.it_block_mask >> 1) ^ (dis->t32.it_block_condition)) & 1 ? "e" : "t"')
								args.append(', dis->t32.it_block_count < 5 ? "" : ( dis->t32.it_block_mask       ^ (dis->t32.it_block_condition)) & 1 ? "e" : "t"')
							elif fun == 'make_wide':
								ins_assert(mode == 'j32')
								print_file(file, indent1 + "\tdis->j32.wide = true;")
								make_wide = True
							elif fun == 'banked_register':
								ins_assert(mode == 'a32' or mode == 't16' or mode == 't32')
								fmtstr += "%s"
								value, _ = parse_variable(mode, arg, varfields)
								args.append(f", a32_banked_register_names[{value}]")
							elif fun == 'banked_spsr':
								ins_assert(mode == 'a32' or mode == 't16' or mode == 't32')
								fmtstr += "%s"
								value, _ = parse_variable(mode, arg, varfields)
								args.append(f", a32_banked_spsr_names[{value}]")
							elif fun == 'fpa_operand':
								# FPA only
								assert mode == 'cdp'
								print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
								fmtstr = ''
								args = []
								print_file(file, indent1 + "\ta32_print_fpa_operand(opcode);")
							elif fun == 'mem_operand':
								assert mode == 'ldc'
								print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
								fmtstr = ''
								args = []
								print_file(file, indent1 + "\ta32_print_ldc_stc_mem_operand(opcode);")
							elif fun == 'positive':
								value, size = parse_variable(mode, arg, varfields)
								fmtstr += '%d'
								value = f"((({value}) - 1) & {(1 << size) - 1}) + 1"
								args.append(f", {value}")
							elif fun == 'vfpop':
								size, value = arg.split(',')

								if '!' in size:
									check = parse_expression(mode, size, varfields, test_only = True)
								else:
									check, bits = parse_variable(mode, size, varfields)
									if bits > 1:
										check = f'({check}) == 0x{(1 << bits) - 1:X}'

								value, _ = parse_variable(mode, value, varfields)
								fmtstr += '%c%d'
								argument = f"{check} ? 'd' : 's'"
								args.append(f", {argument}")
								argument = f"{check} ? {value} : ((({value}) & 0xF) << 1) | (({value}) >> 4)"
								args.append(f", {argument}")
							elif fun == 'bitmask':
								assert mode == 'a64'
								fmtstr += '0x%016"PRIX64"'
								if arg == '0':
									args.append(", a64_get_bitmask32(opcode)")
								elif arg == '1':
									args.append(", a64_get_bitmask64(opcode)")
								else:
									test, _ = parse_variable(mode, arg, varfields, test_only = True)
									args.append(f", {test} ? a64_get_bitmask64(opcode) : a64_get_bitmask32(opcode)")
							elif fun == 'pstate':
								value = parse_expression(mode, arg, varfields, test_only = True)
								fmtstr += '%s'
								args.append(f", a64_pstate_field_names[{value}]")
							elif fun == 'vidx':
								value = parse_expression(mode, arg, varfields)
								fmtstr += '%d'
								args.append(f", arm_get_simd_vector_index({value})")
							elif fun == 'vidx_size':
								value = parse_expression(mode, arg, varfields)
								fmtstr += '%d'
								args.append(f", arm_get_simd_vector_index_size({value})")
							elif fun == 'mask':
								value, mask = arg.split(',')
								value = parse_expression(mode, value, varfields)
								mask = parse_expression(mode, mask, varfields)
								fmtstr += '%d'
								args.append(f", ({value}) & ((1 << ({mask})) - 1)")
							elif fun == 'simd_shift_size':
								value = parse_expression(mode, arg, varfields)
								fmtstr += '%d'
								args.append(f", arm_get_simd_shift_element_size({value})")
							elif fun == 'simd_shift_amount':
								value = parse_expression(mode, arg, varfields)
								fmtstr += '%d'
								args.append(f", arm_get_simd_shift_amount({value})")
							elif fun == 'simd_shift_amount_neg':
								value = parse_expression(mode, arg, varfields)
								fmtstr += '%d'
								args.append(f", arm_get_simd_shift_amount_neg({value})")
							else:
								ins_assert(False, f"Undefined function: {fun}")
						elif '?' in p and not p[:p.find('?')].endswith('!sp'):
							k = p.find('?')
							ins_assert(k != -1)
							l = p.find(':')
							if l == -1 or (l + 1 < len(p) and p[l + 1] == ':'):
								cd_options = p[k + 1:].replace('::', ':').split(';')
								ins_assert(len(cd_options) > 1)
								if all(len(option) == 1 for option in cd_options):
									condition = parse_expression(mode, p[:k], varfields)
									fmtstr += '%c'
									value = f'"{"".join(cd_options)}"[{condition}]'
									args.append(f", {value}")
								else:
									condition, size = parse_variable(mode, p[:k], varfields)
									ins_assert(len(cd_options) <= (1 << size))
									while len(cd_options) < (1 << size):
										cd_options.append("")
									fmtstr += '%s'
									print_file(file, indent1 + f"\tstatic const char * const _names{COUNTER}[] = {{ " + ", ".join('"' + option + '"' for option in cd_options) + " };")
									value = f'_names{COUNTER}[{condition}]'
									COUNTER = COUNTER + 1
									args.append(f", {value}")
							else:
								ins_assert(k < l)
								condition, size = parse_variable(mode, p[:k], varfields, test_only = True)
								if size != 1:
									#print(p, k, l)
									ins_assert(mode == 'a64' or mode == 'a32' or mode == 't32')
									condition, mask = parse_variable(mode, p[:k], varfields, mask_only = True)
									condition = f'({condition}) == 0x{mask:08X}'
								if k + 2 == l and l + 2 == len(p):
									fmtstr += '%c'
									value = f'{condition} ? \'{p[k + 1:l]}\' : \'{p[l + 1:]}\''
									args.append(f", {value}")
								else:
									fmtstr += '%s'
									value = f'{condition} ? \"{p[k + 1:l]}\" : \"{p[l + 1:]}\"'
									args.append(f", {value}")
						else:
							finished = False
							addend = ''
							if p.endswith(':X'):
								# hexadecimal, of default width (8 or 16 hex digits)
								if mode != 'a64':
									fmtstr += '0x%08X'
								else:
									fmtstr += '0x%016"PRIX64"'
								p = p[:-2]
							elif p.endswith(':1X'):
								# hexadecimal of 1 hex digit
								if mode != 'a64':
									fmtstr += '0x%01X'
								else:
									fmtstr += '0x%01"PRIX64"'
								p = p[:-3]
							elif p.endswith(':2X'):
								# hexadecimal of 2 hex digits
								if mode != 'a64':
									fmtstr += '0x%02X'
								else:
									fmtstr += '0x%02"PRIX64"'
								p = p[:-3]
							elif p.endswith(':4X'):
								# hexadecimal of 4 hex digits
								if mode != 'a64':
									fmtstr += '0x%04X'
								else:
									fmtstr += '0x%04"PRIX64"'
								p = p[:-3]
							elif p.endswith(':8X'):
								# hexadecimal of 8 hex digits
								if mode != 'a64':
									fmtstr += '0x%08X'
								else:
									fmtstr += '0x%08"PRIX64"'
								p = p[:-3]
							elif p.endswith('!offset'):
								# program counter relative offset, where the program counter value depends on the current instruction set
								# hexadecimal of 8 or 16 digits
								if mode != 'a64':
									fmtstr += '0x%08X'
								else:
									fmtstr += '0x%016"PRIX64"'
								p = p[:-7]
								if mode == 'a32':
									# For A32 instructions, the program counter points to 8 bytes after the start of the instruction, or 4 bytes after the end of the current one
									addend = '(uint32_t)dis->pc + 4 + '
								elif mode == 't16':
									# For 16-bit Thumb instructions, the program counter points to 4 bytes after the start of the instruction, or 2 bytes after the end of the current one
									addend = '(uint32_t)dis->pc + 2 + '
								elif mode == 't32':
									# For 32-bit Thumb instructions, the program counter points to 4 bytes after the start of the instruction, or to the end of the current one
									addend = '(uint32_t)dis->pc + '
								elif mode == 'a64':
									# For A64 instructions, the program counter points to the current instruction, or 4 bytes before the end of the current one
									addend = 'dis->pc - 4 + '
								elif mode == 'j32':
									# For Java instructions, the program counter points to the current instruction
									addend = '(uint32_t)dis->j32.old_pc + '
								else:
									ins_assert(False)
							elif p.endswith('!shifted_offset'):
								# In A64 mode, the ADRP instruction uses a version of PC that clears the bottom 12 bits
								assert mode == 'a64'
								fmtstr += '0x%016"PRIX64"'
								p = p[:-15]
								addend = '((dis->pc - 4) & ~0xFFF) + '
							elif p.endswith('!mem_offset'):
								# Thumb instructions that use PC relative addressing mask off the lower 2 bits
								assert mode == 't16' or mode == 't32'
								fmtstr += '0x%08X'
								p = p[:-11]
								addend = '(uint32_t)((dis->pc + 2) & ~3) + '
							elif p.endswith('!rol') or p.endswith('!rol+1'):
								# 32-bit VFP registers require the 5-bit register number field to be rotated by 1 bit to the right
								fmtstr += '%d'
								rotate = 1
								addend = 0
								if p.endswith('!rol'):
									p = p[:-4]
								else:
									p = p[:-6]
									addend = 1
								value, size = parse_variable(mode, p, varfields)
								value = f"(((({value}) & {((1 << (size - 1)) - 1)}) << 1) | (({value}) >> {size - 1}))"
								if addend != 0:
									value += f" + {addend}"
								args.append(f", {value}")
								finished = True
							elif p.endswith('!r') or p.endswith('!sp') or '!sp?' in p:
								# For certain A64 instructions, the operation size determines whether a Wn or Xn register is used
								# Also, in certain cases, W31/X31 refer to the zero register (whose value is 0 and writing to it is ignored), others the stack pointer
								ins_assert(mode == 'a64')
								print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
								fmtstr = ''
								args = []
								p, regtype = p.split('!')
								if regtype == 'sp':
									suppress_sp = 'PERMIT_SP'
								elif regtype == 'r':
									suppress_sp = 'SUPPRESS_SP'
								else:
									assert regtype.startswith('sp?')
									suppress_sp = parse_expression(mode, regtype[3:], varfields, test_only = True)
								p, size = p.split('.')
								if size.upper() == '0':
									size = 'false'
								elif size.upper() == '1':
									size = 'true'
								else:
									size, size_size = parse_variable(mode, size, varfields)
									if size_size > 1:
										size = f'({size}) == 0x{(1 << size_size) - 1:X}'
								value = parse_expression(mode, p, varfields)
								print_file(file, indent1 + f"\ta64_print_register_operand({value}, {suppress_sp}, {size});")
								finished = True
							elif p.endswith('!v'):
								# {r.Q.W!v} is shorthand for v{r}.{8<<Q>>W}{W?b;h;s;d}
								ins_assert(mode == 'a64')
								r, cw = p.split('!')[0].split('.')
								r = parse_expression(mode, r, varfields)
								cw = parse_expression(mode, cw, varfields)
								fmtstr += 'v%d.%s'
								args.append(f', {r}')
								args.append(f', a64_vector_suffix[{cw}]')
								finished = True

							else:
								if mode != 'a64':
									fmtstr += '%u'
								else:
									fmtstr += '%"PRIu64"'

							if not finished:
								value = parse_expression(mode, p, varfields)
								if mode == 'a64':
									value = '(uint64_t)(' + value + ')'
								if addend != '':
									value = addend + '(' + value + ')'
								args.append(f", {value}")
						i = j + 1
					else:
						fmtstr += c
				fmtstr += "\\n"

				print_file(file, indent1 + f"\tprintf(\"{fmtstr}\"" + ''.join(args) + ");")
				if mode == 'j32':
					if make_wide:
						print_file(file, indent1 + "\treturn;")

				if 'coproc' in ins:
					indent1 = indent1[:-1]
					print_file(file, indent1 + '\t}')

			elif method == 'step':
				# Generate code for the emulator

				print_file(file, f'#line {ins["begin"][0] + 2} "{DAT_FILE}"')
				for input_line in ins['begin'][1:]:
					output_line = ""
					i = 0
					while i < len(input_line):
						if output_line == '' and input_line[i] == '\t':
							# Initial tabs are converted to the correct indentation
							output_line += indent1 + '\t'
							i += 1
						elif input_line[i:].startswith('${'):
							# Process format string
							j = input_line.find('}', i)
							ins_assert(j != -1)
							p = input_line[i + 2:j]

							if '(' in p:
								# Function call
								fun = p[:p.find('(')]
								arg = p[p.find('(') + 1:-1]
								if fun == 'cond':
									ins_assert(mode == 'a32' or mode == 't32')
									ins_assert(arg == '')
									if mode == 'a32':
										output_line += 'true'
									elif mode == 't32':
										output_line += 't32_check_condition(cpu)'
								elif fun == 'operand':
									if mode == 'a32' or mode == 't32':
										if arg == '' or arg == '0':
											store_carry = 'false'
										elif arg == '1':
											store_carry = 'true'
										else:
											store_carry = str(parse_expression(mode, arg, varfields, test_only = True)).lower()
										if mode == 'a32':
											output_line += f'a32_get_operand(cpu, opcode, {store_carry})'
										elif mode == 't32':
											output_line += f't32_get_register_operand(cpu, opcode1, opcode2, {store_carry})'
									else:
										ins_assert(False)
								elif fun == 'simd_operand':
									ins_assert(mode == 'a32' or mode == 't32' or mode == 'a64')
									if mode == 'a32':
										output_line += 'a32_get_simd_operand(cpu, opcode)'
									elif mode == 't32':
										output_line += 't32_get_simd_operand(cpu, opcode1, opcode2)'
									elif mode == 'a64':
										output_line += 'a64_get_simd_operand(cpu, opcode)'
								elif fun == 'adr_operand':
									ins_assert(mode == 'a32')
									output_line += 'a32_get_address_operand(cpu, opcode)'
								elif fun == 'pcoffset':
									ins_assert(mode == 'a32')
									regnum = parse_expression(mode, arg, varfields)
									output_line += f'({regnum} == A32_PC_NUM ? a32_get_register_shift_offset_for_pc(cpu, opcode) : 0)'
								elif fun == 'immed':
									ins_assert(mode == 't32')
									output_line += "t32_get_immediate_operand(opcode1, opcode2)"
								elif fun == 't32label':
									ins_assert(mode == 't32')
									fields = [(0, 10, 1), (0, 9, 0), (11, 11, 1), (13, 13, 1), (10, 10, 0)]
									initial_shift = 1
									value = get_value('t32', None, fields, initial_shift = initial_shift)
									size = get_value_size('t32', fields, initial_shift = initial_shift)
									output_line += f'(cpu->r[PC] + sign_extend({size}, ({value}) ^ ((uint32_t)(~opcode2 & 0x0400) << 12) ^ ((uint32_t)(~opcode2 & 0x0400) << 13)))'
									#print_file(file, f"printf(\"%08X\\n\", ({value}) ^ ((uint32_t)(~opcode2 & 0x0400) << 12) ^ ((uint32_t)(~opcode2 & 0x0400) << 13));")
								elif fun == 'positive':
									value, size = parse_variable(mode, arg, varfields)
									output_line += f"((({value}) - 1) & {(1 << size) - 1}) + 1"
								elif fun == 'fpa_operand':
									# FPA only
									assert mode == 'cdp'
									output_line += 'a32_fetch_fpa_operand(cpu, opcode)'
								elif fun == 'bitmask':
									assert mode == 'a64'
									if arg == '0':
										output_line += "a64_get_bitmask32(opcode)"
									elif arg == '1':
										output_line += "a64_get_bitmask64(opcode)"
									else:
										test, _ = parse_variable(mode, arg, varfields, test_only = True)
										output_line += f"{test} ? a64_get_bitmask64(opcode) : a64_get_bitmask32(opcode)"
								elif fun == 'dveclen' or fun == 'sveclen':
									assert mode in COPROC_ISAS
									arg, _ = parse_variable(mode, arg, varfields, test_only = arg)
									if fun == 'dveclen':
										test = f"({arg} & 0x1C) == 0"
									elif fun == 'sveclen':
										test = f"({arg} & 0x0C) == 0" # unrotated
									output_line += f"({test} ? (cpu->vfp.fpscr & FPSCR_LEN_MASK) >> FPSCR_LEN_SHIFT : 1)"
								else:
									ins_assert(False, f"Undefined function: {fun}")
							else:
								test_only = False
								addend = ''
								get_size = False
								rotate = 0
								if p.endswith('!test'):
									p = p[:-5]
									test_only = True
								elif p.endswith('!offset'):
									p = p[:-7]
									if mode == 'j32':
										addend = 'cpu->old_pc + '
									else:
										addend = '$pc + '
								elif p.endswith('!shifted_offset'):
									assert mode == 'a64'
									p = p[:-15]
									addend = '($pc & ~0xFFF) + '
								elif p.endswith('!size'):
									ins_assert(mode == 'j32')
									p = p[:-5]
									get_size = True
								elif p.endswith('!rol'):
									rotate = 1
									p = p[:-4]

								if get_size:
									_, size = parse_variable(mode, p, varfields)
									value = str(size)
								elif rotate != 0:
									value, size = parse_variable(mode, p, varfields)
									value = f"(((({value}) & {((1 << (size - 1)) - 1)}) << 1) | (({value}) >> {size - 1}))"
								else:
									value = parse_expression(mode, p, varfields, test_only = test_only, parentheses = True)

								value = addend + value
								output_line += value
							i = j + 1
						else:
							output_line += input_line[i]
							i += 1

					# Replace shorthands

					replacements = {}

					if mode == 'a64':
						replacements["$w[]"] = 'a64_register_get32(cpu, $?, true)'
						replacements["$w[]="] = 'a64_register_set32(cpu, $?, true, $$)'
						replacements["$w|sp[]"] = 'a64_register_get32(cpu, $?)' # takes 2 arguments
						replacements["$w|sp[]="] = 'a64_register_set32(cpu, $?, $$)' # takes 2 arguments
						replacements["$x[]"] = 'a64_register_get64(cpu, $?, true)'
						replacements["$x[]="] = 'a64_register_set64(cpu, $?, true, $$)'
						replacements["$x|sp[]"] = 'a64_register_get64(cpu, $?)' # takes 2 arguments
						replacements["$x|sp[]="] = 'a64_register_set64(cpu, $?, $$)' # takes 2 arguments
						replacements["$pc"] = '(cpu->r[PC] - 4)'
					else:
						replacements["$r[]"] = 'a32_register_get32(cpu, $?)'
						replacements["$r[]="] = 'a32_register_set32(cpu, $?, $$)'
						replacements["$r.v5[]="] = 'a32_register_set32_interworking_v5(cpu, $?, $$)'
						replacements["$r.v7[]="] = 'a32_register_set32_interworking_v7(cpu, $?, $$)'
						replacements["$r.lhs[]"] = 'a32_register_get32_lhs(cpu, $?)' # in A26, R15 will contain the CPSR flags as well
						replacements["$r.str[]"] = 'a32_register_get32_str(cpu, $?)' # R15 might have 4 more bytes of displacement
						replacements["$lr="] = 'a32_register_set32(cpu, A32_LR, $$)'
						replacements["$lr"] = 'a32_register_get32(cpu, A32_LR)'
						replacements["$pc.cpsr"] = 'a32_register_get32_lhs(cpu, A32_PC_NUM)'
						replacements["$pc_next.cpsr"] = 'a32_register_get32_lhs(cpu, A32_PC_NUM)'
						replacements["$pc"] = 'a32_register_get32(cpu, A32_PC_NUM)'
						replacements["$spsr"] = 'a32_get_spsr(cpu)'

					if mode != 'a64':
						replacements["$c[]"] = 'a32_check_condition(cpu, $?)'
						replacements["$pc="] = 'a32_set_pc(cpu, $$)'
					else:
						replacements["$c[]"] = 'a64_check_condition(cpu, $?)'
						replacements["$pc="] = '(cpu->r[PC] = ($$))'
					replacements["$pc_next"] = '(cpu->r[PC] & 0xFFFFFFFF)'
					#replacements["$cpsr="] = 'set_cpsr(cpu, $$)'
					replacements["$cpsr.mode="] = '(cpu->pstate.mode = $$)'
					replacements["$cpsr.thumb="] = '(cpu->pstate.jt = ($$ ? PSTATE_JT_THUMB : PSTATE_JT_ARM))'
					replacements["$cpsr.thumbee="] = '(cpu->pstate.jt = ($$ ? PSTATE_JT_THUMBEE : PSTATE_JT_THUMB))'
					replacements["$cpsr.f="] = '(cpu->pstate.f = $$)'
					replacements["$cpsr.i="] = '(cpu->pstate.i = $$)'
					replacements["$cpsr.a="] = '(cpu->pstate.a = $$)'
					replacements["$cpsr.e="] = '(cpu->pstate.e = $$)'
					replacements["$cpsr"] = 'a32_get_cpsr(cpu)'
					replacements["$cpsr.ge0"] = '(cpu->pstate.ge & 1)'
					replacements["$cpsr.ge1"] = '((cpu->pstate.ge >> 1) & 1)'
					replacements["$cpsr.ge2"] = '((cpu->pstate.ge >> 2) & 1)'
					replacements["$cpsr.ge3"] = '((cpu->pstate.ge >> 3) & 1)'
					if mode != 'a64':
						replacements["$cpsr.nzcv="] = 'a32_set_cpsr_nzcv(cpu, $$)'
					else:
						replacements["$nzcv="] = 'a32_set_cpsr_nzcv(cpu, $$)'
					replacements["$old_pc"] = 'cpu->old_pc'

					if mode == 't16' or mode == 't32':
						replacements["$itstate="] = 't32_set_it_state(cpu, $$)'

					if mode in COPROC_ISAS:
						replacements["$s[]"] = 'a32_register_get32fp(cpu, $?, 0)'
						replacements["$s[]="] = 'a32_register_set32fp(cpu, $?, 0, $$)'
						replacements["$d[]"] = 'a32_register_get64fp(cpu, $?, 0)'
						replacements["$d[]="] = 'a32_register_set64fp(cpu, $?, 0, $$)'

						# vector
						replacements["$s.v[]"] = 'a32_register_get32fp(cpu, $?)' # takes 2 parameters: base, index
						replacements["$s.v[]="] = 'a32_register_set32fp(cpu, $?, $$)' # takes 2 parameters: base, index
						replacements["$d.v[]"] = 'a32_register_get64fp(cpu, $?)' # takes 2 parameters: base, index
						replacements["$d.v[]="] = 'a32_register_set64fp(cpu, $?, $$)' # takes 2 parameters: base, index

						# maybe vector
						replacements["$s.?[]"] = 'a32_register_get32fp_maybe_vector(cpu, $?)' # takes 2 parameters: base, index
						replacements["$s.?[]="] = 'a32_register_set32fp_maybe_vector(cpu, $?, $$)' # takes 2 parameters: base, index
						replacements["$d.?[]"] = 'a32_register_get64fp_maybe_vector(cpu, $?)' # takes 2 parameters: base, index
						replacements["$d.?[]="] = 'a32_register_set64fp_maybe_vector(cpu, $?, $$)' # takes 2 parameters: base, index

						replacements["$d.low[]"] = 'a32_register_get64fp_low(cpu, $?)'
						replacements["$d.low[]="] = 'a32_register_set64fp_low(cpu, $?, $$)'
						replacements["$d.high[]"] = 'a32_register_get64fp_high(cpu, $?)'
						replacements["$d.high[]="] = 'a32_register_set64fp_high(cpu, $?, $$)'
						replacements["$d.both[]"] = 'a32_register_get64fp_both(cpu, $?)'
						replacements["$d.both[]="] = 'a32_register_set64fp_both(cpu, $?, $$)'

					if mode == 'ldc':
						replacements['$address'] = 'address'
					elif mode == 'mcr':
						replacements['$operand'] = 'value'
					elif mode == 'mcrr':
						replacements['$operand.l'] = 'value1'
						replacements['$operand.h'] = 'value2'
						replacements['$operand'] = '(((uint64_t)value2 << 32) | value1)'
					elif mode == 'mrc':
						replacements['$result='] = '(result = $$)'
					elif mode == 'mrrc':
						replacements['$result.l='] = '(result.l = $$)'
						replacements['$result.h='] = '(result.h = $$)'

					output_line = replace_placeholders(output_line, replacements)
					print_file(file, output_line)
				print_file(file, f'#line {LINE_NUMBER + 2} "{GEN_FILE}"')

		if len(options) > 1:
			print_file(file, indent + "\t}")

		if mode in COPROC_ISAS:
			if method == 'parse':
				print_file(file, indent + "\treturn true;")
			elif mode == 'mrc' or mode == 'mrrc':
				print_file(file, indent + "\treturn result;")

		print_file(file, indent + "}")
		else_kwd = 'else '

	if method == 'parse':
		if else_kwd != '':
			print_file(file, indent + f"{else_kwd.strip()}")
		print_file(file, indent + "{")
		if mode in COPROC_ISAS:
			print_file(file, indent + "\treturn false;")
		elif mode == 'j32':
			tablen = TABLEN - 4
			print_file(file, indent + "\tprintf(\">" + ("\\t" * ((tablen + 7) // 8)) + "?\\n\");")
		else:
			print_file(file, indent + "\tprintf(\"?\\n\");")
		print_file(file, indent + "}")

		if mode == 'j32':
			print_file(file, indent + "dis->j32.wide = false;")

	if method == 'step':
		if mode == 'a32':
			print_file(file, indent + f'{else_kwd}if(cpu->config.version >= ARMV4)')
		elif else_kwd != '':
			print_file(file, indent + f"{else_kwd.rstrip()}")

		print_file(file, indent + "{")

		if mode == 'j32':
			print_file(file, indent + "\tj32_break(cpu, opcode);")

		elif mode in COPROC_ISAS or mode in {'a32', 'a64', 't16', 't32', 'j32'}:
			print_file(file, indent + "\tarm_undefined(cpu);")

		print_file(file, indent + "}")

def generate_all(method, file):
	global LINE_NUMBER

	assert method in {'parse', 'step'}

	if method == 'parse':
		cpu = 'dis'
		args = "arm_parser_state_t * dis"
		a32_fetch = 'file_fetch'
		j32_fetch = 'file_fetch'
		a64_fetch = 'file_fetch'
	elif method == 'step':
		cpu = 'cpu'
		args = "arm_state_t * cpu"
		a32_fetch = 'a32_fetch'
		j32_fetch = 'j32_fetch'
		a64_fetch = 'a64_fetch'
	else:
		assert False

	#### Coprocessors

	if method == 'parse':

		for key, order in [
			('cdp', cdp_order),
			('ldc', ldc_order),
			('mcr', mcr_order),
			('mrc', mrc_order),
			('mcrr', mcrr_order),
			('mrrc', mrrc_order)
		]:
			print_file(file, f'bool {key}_parse(arm_parser_state_t * dis, uint32_t opcode, uint8_t current_condition)')
			print_file(file, '{')

			generate_branches(key, order, '\t', method)

			print_file(file, "}")

	elif method == 'step':

		for key, order, coprocs in [
			('cdp', cdp_order, cdp_coproc),
			('ldc_stc', ldc_order, ldc_coproc),
			('mcr', mcr_order, mcr_coproc),
			('mrc', mrc_order, mrc_coproc),
			('mcrr', mcrr_order, mcrr_coproc),
			('mrrc', mrrc_order, mrrc_coproc)
		]:
			for cpnum in coprocs:
				arguments = ""
				return_type = "void"
				if key == 'ldc_stc':
					arguments = ", uint32_t address"
				elif key == 'mcr':
					arguments = ", uint32_t value"
				elif key == 'mcrr':
					arguments = ", uint32_t value1, uint32_t value2"
				elif key == 'mrc':
					return_type = "uint32_t"
				elif key == 'mrrc':
					return_type = "uint32_pair_t"

				print_file(file, f'{return_type} cp{cpnum}_perform_{key}(arm_state_t * cpu, uint32_t opcode{arguments})')
				print_file(file, '{')
				if return_type != 'void':
					print_file(file, f'\t{return_type} result;')

				if key == 'ldc_stc':
					mode = 'ldc'
				else:
					mode = key
				generate_branches(mode, order, '\t', method, cpnum = cpnum)

				print_file(file, "}")

	#### A32

	print_file(file, f'void a32_{method}({args}{", bool is_arm26" if method == "parse" else ""})')
	print_file(file, '{')
	if method == 'parse':
		print_file(file, '\tuint32_t old_pc = dis->pc;')
	elif method == 'step':
		print_file(file, '\tif(setjmp(cpu->exc))')
		print_file(file, '\t\treturn;')
		print_file(file, '\tcpu->old_pc = cpu->r[PC];')

	print_file(file, f'\tuint32_t opcode = {a32_fetch}32({cpu});')
	if method == 'parse':
		print_file(file, '\tif(opcode == 0)')
		print_file(file, '\t{')
		print_file(file, '\t\tswitch(dis->input_null_count++)')
		print_file(file, '\t\t{')
		print_file(file, '\t\tcase 0:')
		print_file(file, '\t\t\tbreak;')
		print_file(file, '\t\tcase 1:')
		print_file(file, '\t\t\tprintf("...\\n");')
		print_file(file, '\t\t\treturn;')
		print_file(file, '\t\tdefault:')
		print_file(file, '\t\t\treturn;')
		print_file(file, '\t\t}')
		print_file(file, '\t}')
		print_file(file, '\telse')
		print_file(file, '\t{')
		print_file(file, '\t\tdis->input_null_count = 0;')
		print_file(file, '\t}')

		print_file(file, '\tprintf("[%08X]\\t", old_pc);')
		print_file(file, f'\tprintf("<%08X>\\t", opcode);')

	generate_branches('a32', a32_order, '\t', method)

	print_file(file, "}")

	#### A64

	print_file(file, f"void a64_{method}({args})")
	print_file(file, '{')
	if method == 'parse':
		print_file(file, '\tuint64_t old_pc = dis->pc;')
	elif method == 'step':
		print_file(file, '\tif(setjmp(cpu->exc))')
		print_file(file, '\t\treturn;')
		print_file(file, '\tcpu->old_pc = cpu->r[PC];')

	print_file(file, f'\tuint32_t opcode = {a64_fetch}32({cpu});')
	if method == 'parse':
		print_file(file, '\tif(opcode == 0)')
		print_file(file, '\t{')
		print_file(file, '\t\tswitch(dis->input_null_count++)')
		print_file(file, '\t\t{')
		print_file(file, '\t\tcase 0:')
		print_file(file, '\t\t\tbreak;')
		print_file(file, '\t\tcase 1:')
		print_file(file, '\t\t\tprintf("...\\n");')
		print_file(file, '\t\t\treturn;')
		print_file(file, '\t\tdefault:')
		print_file(file, '\t\t\treturn;')
		print_file(file, '\t\t}')
		print_file(file, '\t}')
		print_file(file, '\telse')
		print_file(file, '\t{')
		print_file(file, '\t\tdis->input_null_count = 0;')
		print_file(file, '\t}')

		print_file(file, '\tprintf("[%016"PRIX64"]\\t", old_pc);')
		print_file(file, '\tprintf("<%08X>\\t", opcode);')

	generate_branches('a64', a64_order, '\t', method)

	print_file(file, "}")

	#### T32

	print_file(file, f"void t32_{method}({args}{', bool is_thumbee' if method == 'parse' else ''})")
	print_file(file, '{')

	if method == 'parse':
		print_file(file, '\tuint32_t old_pc = dis->pc;')
	elif method == 'step':
		print_file(file, '\tif(setjmp(cpu->exc))')
		print_file(file, '\t{')
		print_file(file, '\t\tt32_set_it_state(cpu, 0);')
		print_file(file, '\t\treturn;')
		print_file(file, '\t}')
		print_file(file, '\tcpu->old_pc = cpu->r[PC];')
	print_file(file, f'\tuint16_t opcode1 = {a32_fetch}16({cpu});')
	is_thumb2 = f'(({cpu}->config.features & (1 << FEATURE_THUMB2)) || (({cpu}->config.features & FEATURE_PROFILE_MASK) == ARM_PROFILE_M))'
	print_file(file, f'\tif({is_thumb2} && ((opcode1 & 0xF800) == 0xE800 || (opcode1 & 0xF800) == 0xF000 || (opcode1 & 0xF800) == 0xF800))')
	print_file(file, '\t{')
	print_file(file, f'\t\tuint16_t opcode2 = {a32_fetch}16({cpu});')
	if method == 'parse':
		print_file(file, '\t\tif(opcode1 == 0 && opcode2 == 0)')
		print_file(file, '\t\t{')
		print_file(file, '\t\t\tswitch(dis->input_null_count++)')
		print_file(file, '\t\t\t{')
		print_file(file, '\t\t\tcase 0:')
		print_file(file, '\t\t\t\tbreak;')
		print_file(file, '\t\t\tcase 1:')
		print_file(file, '\t\t\t\tprintf("...\\n");')
		print_file(file, '\t\t\t\treturn;')
		print_file(file, '\t\t\tdefault:')
		print_file(file, '\t\t\t\treturn;')
		print_file(file, '\t\t\t}')
		print_file(file, '\t\t}')
		print_file(file, '\t\telse')
		print_file(file, '\t\t{')
		print_file(file, '\t\t\tdis->input_null_count = 0;')
		print_file(file, '\t\t}')

		print_file(file, '\t\tprintf("[%08X]\\t", old_pc);')
		print_file(file, '\t\tprintf("<%04X %04X>\\t", opcode1, opcode2);')

	#### T32, 32-bit

	generate_branches('t32', t32_order, '\t\t', method)

	print_file(file, '\t}')
	print_file(file, '\telse')
	print_file(file, '\t{')
	if method == 'parse':
		print_file(file, '\t\tif(opcode1 == 0)')
		print_file(file, '\t\t{')
		print_file(file, '\t\t\tswitch(dis->input_null_count++)')
		print_file(file, '\t\t\t{')
		print_file(file, '\t\t\tcase 0:')
		print_file(file, '\t\t\t\tbreak;')
		print_file(file, '\t\t\tcase 1:')
		print_file(file, '\t\t\t\tprintf("...\\n");')
		print_file(file, '\t\t\t\treturn;')
		print_file(file, '\t\t\tdefault:')
		print_file(file, '\t\t\t\treturn;')
		print_file(file, '\t\t\t}')
		print_file(file, '\t\t}')
		print_file(file, '\t\telse')
		print_file(file, '\t\t{')
		print_file(file, '\t\t\tdis->input_null_count = 0;')
		print_file(file, '\t\t}')

		print_file(file, '\t\tprintf("[%08X]\\t", old_pc);')
		print_file(file, '\t\tprintf("<%04X>\\t\\t", opcode1);')

	#### T32, 16-bit

	generate_branches('t16', t16_order, '\t\t', method)

	print_file(file, "\t}")

	if method == 'parse':
		print_file(file, "\tif(dis->t32.it_block_count > 0)")
		print_file(file, "\t{")
		print_file(file, "\t\tdis->t32.it_block_count --;")
		print_file(file, "\t\tdis->t32.it_block_mask <<= 1;")
		print_file(file, "\t\tif(dis->t32.it_block_count == 0)")
		print_file(file, "\t\t\tdis->t32.it_block_condition = COND_ALWAYS;")
		print_file(file, "\t}")
	elif method == 'step':
		print_file(file, "\tt32_advance_it(cpu);")

	print_file(file, "}")

	#### Java

	print_file(file, f"void j32_{method}({args})")
	print_file(file, '{')
	if method == 'parse':
		print_file(file, '\tuint32_t old_pc = dis->pc;')
	elif method == 'step':
		print_file(file, '\tif(setjmp(cpu->exc))')
		print_file(file, '\t\treturn;')
		print_file(file, '\tcpu->old_pc = cpu->r[PC];')
	if method == 'parse':
		print_file(file, '\tif(dis->j32.parse_state_count <= 0)')
		print_file(file, '\t{')
		print_file(file, '\t\tdis->j32.parse_state = J32_PARSE_INS;')
		print_file(file, '\t\tdis->j32.parse_state_count = 0;')
		print_file(file, '\t}')
		print_file(file, '\tswitch(dis->j32.parse_state)')
		print_file(file, '\t{')
		print_file(file, '\tcase J32_PARSE_INS:')
		print_file(file, '\t\tbreak;')
		print_file(file, '\tcase J32_PARSE_LINE:')
		print_file(file, '\t\t{')
		print_file(file, '\t\t\tprintf("[%08X]\\t", old_pc);')
		print_file(file, '\t\t\tint32_t offset = file_fetch32be(dis);')
		print_file(file, '\t\t\tprintf("<%02X %02X %02X %02X>' + '\\t' * ((TABLEN - 13 + 7) // 8) + '%08X\\n", (offset >> 24) & 0xFF, (offset >> 16) & 0xFF, (offset >> 8) & 0xFF, offset & 0xFF, dis->j32.old_pc + offset);')
		print_file(file, '\t\t\tdis->j32.parse_state_count --;')
		print_file(file, '\t\t}')
		print_file(file, '\t\treturn;')
		print_file(file, '\tcase J32_PARSE_PAIR:')
		print_file(file, '\t\t{')
		print_file(file, '\t\t\tprintf("[%08X]\\t", old_pc);')
		print_file(file, '\t\t\tint32_t match = file_fetch32be(dis);')
		print_file(file, '\t\t\tint32_t offset = file_fetch32be(dis);')
		print_file(file, '\t\t\tprintf("<%02X %02X %02X %02X %02X %02X %02X %02X>\\n' + '\\t' * (2 + TABLEN // 8) + '%08X: %08X\\n", (match >> 24) & 0xFF, (match >> 16) & 0xFF, (match >> 8) & 0xFF, match & 0xFF, (offset >> 24) & 0xFF, (offset >> 16) & 0xFF, (offset >> 8) & 0xFF, offset & 0xFF, match, dis->j32.old_pc + offset);')
		print_file(file, '\t\t\tdis->j32.parse_state_count --;')
		print_file(file, '\t\t}')
		print_file(file, '\t\treturn;')
		print_file(file, '\t}')
	print_file(file, '\tuint16_t opcode;')
	if method == 'step':
		print_file(file, '\tbool j32_wide = false;')
		print_file(file, 'restart:')
	if method == 'parse':
		print_file(file, '\tdis->j32.old_pc = dis->pc;')
	elif method == 'step':
		print_file(file, '\tcpu->old_pc = cpu->r[PC];')

	print_file(file, f'\topcode = {j32_fetch}8({cpu}) & 0xFF;')
	if method == 'parse':
		print_file(file, f'\tif(({cpu}->config.jazelle_implementation >= ARM_JAVA_PICOJAVA && opcode == 0xFF) || ({cpu}->config.jazelle_implementation >= ARM_JAVA_EXTENSION && opcode == 0xFE))')
	elif method == 'step':
		print_file(file, f'\tif({cpu}->config.jazelle_implementation >= ARM_JAVA_EXTENSION && (opcode == 0xFF || opcode == 0xFE))')
	print_file(file, '\t{')
	print_file(file, f'\t\topcode = (opcode << 8) | ({j32_fetch}8({cpu}) & 0xFF);')
	print_file(file, '\t}')

	if method == 'parse':
		print_file(file, '\tif(opcode == 0)')
		print_file(file, '\t{')
		print_file(file, '\t\tswitch(dis->input_null_count++)')
		print_file(file, '\t\t{')
		print_file(file, '\t\tcase 0:')
		print_file(file, '\t\t\tbreak;')
		print_file(file, '\t\tcase 1:')
		print_file(file, '\t\t\tprintf("...\\n");')
		print_file(file, '\t\t\treturn;')
		print_file(file, '\t\tdefault:')
		print_file(file, '\t\t\treturn;')
		print_file(file, '\t\t}')
		print_file(file, '\t}')
		print_file(file, '\telse')
		print_file(file, '\t{')
		print_file(file, '\t\tdis->input_null_count = 0;')
		print_file(file, '\t}')

		print_file(file, '\tprintf("[%08X]\\t", old_pc);')
		print_file(file, '\tif(opcode >= 0x100)')
		print_file(file, '\t\tprintf("<%02X %02X", opcode >> 8, opcode & 0xFF);')
		print_file(file, '\telse')
		print_file(file, '\t\tprintf("<%02X", opcode);')

	generate_branches('j32', j32_order, '\t', method)

	print_file(file, "}")

if PARSE_FILE is not None:
	GEN_FILE = PARSE_FILE
	with open(GEN_FILE, 'w') as file:
		generate_all('parse', file)

LINE_NUMBER = 0

if STEP_FILE is not None:
	GEN_FILE = STEP_FILE
	with open(GEN_FILE, 'w') as file:
		generate_all('step', file)

#### Experimenting with automatic HTML generation

if HTML_FILE is not None:
	with open(HTML_FILE, 'w') as file:
		print("""<!-- This file is automatically generated -->
<!doctype html>
<html>
<head>
<title>Instruction sets</title>
<style>
td {
	text-align: center;
}
</style>
</head>
<body>""", file = file)
		for mode, order, name in [
				('a32', a32_order, "ARM instructions"),
				('t16', t16_order, "16-bit Thumb instructions"),
				('t32', t32_order, "32-bit Thumb instructions"),
				('a64', a64_order, "A64 instructions"),
# TODO: coprocessor instructions
			]:
			print(f"<h1>{name}</h1>", file = file)
			print("<table align='center' border='1'>", file = file)
			print("<tr>", file = file)
			print("<td></td>", file = file)
			for i in range(16 if mode == 't16' else 32):
				if mode == 't16':
					bit = 15 - i
				elif mode == 't32':
					bit = 15 - (i % 16)
				else:
					bit = 31 - i
				print(f"<td width='18'>{'<br>'.join(str(bit))}</td>", file = file)
			print("<td></td>", file = file)
			print("</tr>", file = file)
			for _, ins in sorted(order.items(), key = lambda pair: (pair[0][-1],) + pair[0][:-1]):
				print("<tr>", file = file)
				if 'removed' in ins:
					removed = f"<hr>{ins['removed']}"
				else:
					removed = ""
				print(f"<td>{ins['added']}{removed}</td>", file = file)
				last_code = None
				code_count = 0
				for c in ins['code'] + '^':
					if c == ' ':
						continue
					elif last_code == c:
						code_count += 1
					else:
						if last_code is not None:
							if code_count > 1:
								colspan = f" colspan='{code_count}'"
							else:
								colspan = ""
							print(f"<td{colspan}>{last_code}</td>", file = file)
							last_code = None
						if c in {'0', '1'}:
							print(f"<td>{c}</td>", file = file)
						elif c in {'@', '!'}:
							c2 = {'@': '0', '!': '1'}[c]
							print(f"<td style='background:lightgray;'>{c2}</td>", file = file)
						elif c == '.':
							print(f"<td style='background:lightgray;'></td>", file = file)
						else:
							last_code = c
							code_count = 1
				texts = []
				for asm in [ins['asm']] + ([ins['ual']] if 'ual' in ins else []):
					texts.append("")
					i = 0
					while i < len(asm):
						if asm[i] == '{':
							j = asm.find('}', i)
							param = asm[i + 1:j]
							if param in {'cond()', 'cond(c)'}:
								param = '<i>c</i>'
							elif param == 'cond_or_s()':
								param = '[<i>c</i>|S]'
							else:
								param = '<i>' + param.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;') + '</i>'
							# TODO: other cases
							texts[-1] += param
							i = j + 1
						else:
							texts[-1] += asm[i].upper()
							i += 1
				print(f"<td>{'<br>'.join(texts)}</td>", file = file)
				print("</tr>", file = file)
			print("</table>", file = file)
		print("""</body>
</html>""", file = file)

