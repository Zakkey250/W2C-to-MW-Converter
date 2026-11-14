"""Close selected Carbon smackables over sequencer-created child attributes.

Geometry selection alone misses attributes for pieces spawned at destruction
time. Read dependencies from the supplied source, never from guessed MW types.
"""
import struct
from audit_breaker_sequences import commands


def collect_breaker_dependencies(rows, engines, roots, native_engines=()):
    roots = set(roots)
    native_engines = set(native_engines)
    rows = {(r['class'], r['name']): r for r in rows}
    by_hash = {int(r['hash'], 16): r['name'] for (cls, _), r in rows.items()
               if cls == 'smackable'}

    def effective(name, visiting=None):
        visiting = set() if visiting is None else visiting
        if name in visiting:
            raise ValueError(f'Attribute inheritance cycle: {name}')
        row = rows.get(('smackable', name))
        if row is None:
            raise ValueError(f'Missing source smackable: {name}')
        visiting.add(name)
        result = effective(row['parent'], visiting) if row['parent'] else {}
        visiting.remove(name)
        return result | row['fields']

    pending = list(roots)
    selected, used_engines, preserved_native, edges = set(), set(), set(), []
    while pending:
        name = pending.pop()
        if name in selected:
            continue
        selected.add(name)
        engine = effective(name).get('EventSequencer', '')
        if not engine:
            continue
        # Generic pieces keep MW's existing engine. Importing Carbon's entire
        # generic engine here would replace shared behavior and broaden scope.
        # Explicit roots remain eligible for a separately namespaced port.
        if engine in native_engines and name not in roots:
            preserved_native.add(engine)
            continue
        if engine not in engines:
            raise ValueError(f'Missing source sequencer: {name}/{engine}')
        used_engines.add(engine)
        for kind, size, data in commands(engines[engine]):
            if kind != 0x2554C48A:
                continue
            if size != 48:
                raise ValueError(f'Unexpected fragment size: {engine}/{size}')
            child = struct.unpack_from('<I', data, 12)[0]
            if not child:
                continue
            if child not in by_hash:
                raise ValueError(f'Missing source fragment attribute: {engine}/{child:08X}')
            target = by_hash[child]
            edge = dict(owner=name, engine=engine, child=target, sourceHash=f'{child:08X}')
            if edge not in edges:
                edges.append(edge)
            pending.append(target)
    return dict(roots=sorted(set(roots)), smackables=sorted(selected),
                engines=sorted(used_engines), preservedNativeEngines=sorted(preserved_native),
                fragmentReferences=edges)
