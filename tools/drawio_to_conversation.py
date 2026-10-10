#!/usr/bin/env python3
"""Convert annotated draw.io graphs to the game's TOML conversation format."""
import argparse
import base64
import html
from html.parser import HTMLParser
import json
import math
from pathlib import Path
import urllib.parse
import xml.etree.ElementTree as ET
import zlib

class PlainText(HTMLParser):
    def __init__(self):
        super().__init__(); self.parts = []
    def handle_data(self, value): self.parts.append(value)
    def handle_starttag(self, tag, attrs):
        if tag in ('br', 'p', 'div') and self.parts: self.parts.append('\n')

def plain(value):
    parser = PlainText(); parser.feed(html.unescape(value or ''))
    return ''.join(parser.parts).strip()

def boolean(value):
    if value.lower() not in ('true', 'false'): raise ValueError('Boolean properties must be true or false')
    return value.lower() == 'true'

def properties(attrs):
    out = {}
    for key in ('requires_flag', 'set_flag', 'declaration', 'requires', 'check', 'action', 'reply', 'outcome'):
        if key in attrs: out[key] = attrs[key]
    for key in ('effects','test','requires_test'):
        if key in attrs: out[key] = json.loads(attrs[key])
    for key in ('requires_value', 'set_value', 'check_value', 'restart', 'stop', 'reveal_contacts', 'reveal_interface', 'show_draft', 'record_instruction', 'show_task_document', 'important'):
        if key in attrs: out[key] = boolean(attrs[key])
    for key in ('document_index','within_seconds','after_seconds','review_trust_delta'):
        if key in attrs:
            out[key] = int(attrs[key]) if key=='document_index' else float(attrs[key])
            if not math.isfinite(out[key]): raise ValueError('Seconds must be finite')
    for key in ('unlock','reveal_tabs'):
        if key in attrs:
            value = attrs[key].strip()
            out[key] = json.loads(value) if value.startswith('[') else [s.strip() for s in value.split(',') if s.strip()]
            if not isinstance(out[key], list) or not all(isinstance(s,str) for s in out[key]): raise ValueError(key+' must contain names')
    return out

def finite(value):
    return isinstance(value,(float,int)) and not isinstance(value,bool) and math.isfinite(value)

def validate_variables(variables):
    if not isinstance(variables,dict): raise ValueError('variables must be a JSON object')
    for name,spec in variables.items():
        if not name or not isinstance(spec,dict) or not finite(spec.get('initial')): raise ValueError('Variable needs a finite initial value')
        for key in ('min','max'):
            if key in spec and not finite(spec[key]): raise ValueError('Invalid variable bound')
        if 'integer' in spec and not isinstance(spec['integer'],bool): raise ValueError('integer must be boolean')
        initial=spec['initial']
        if not spec.get('min',-math.inf)<=initial<=spec.get('max',math.inf): raise ValueError('Initial value outside bounds')
        if spec.get('integer') and initial!=int(initial): raise ValueError('Counter must be integral')

def validate_rules(entry,variables,choice):
    def operand(rule):
        def reference(name):
            if not isinstance(name,str): raise ValueError('Invalid state reference')
            if name in variables: return
            if '.' not in name or not all(name.rsplit('.',1)): raise ValueError('Use character_id.field for state references')
        if not isinstance(rule,dict): raise ValueError('Rule must be a table')
        reference(rule.get('variable'))
        if ('value' in rule)==('other' in rule): raise ValueError('Choose a numeric value or another variable')
        if 'value' in rule and not finite(rule['value']): raise ValueError('Invalid numeric operand')
        if 'other' in rule: reference(rule['other'])
    for key in ('test','requires_test'):
        if key in entry:
            rule=entry[key];operand(rule)
            if rule.get('op') not in ('lt','le','eq','ne','ge','gt'): raise ValueError('Unknown comparison operator')
    if 'test' in entry and (choice or entry.get('kind')!='condition'): raise ValueError('test belongs on a condition node')
    if 'effects' in entry:
        if not isinstance(entry['effects'],list): raise ValueError('effects must be an array')
        for rule in entry['effects']:
            operand(rule)
            if rule.get('op') not in ('add','set'): raise ValueError('Unknown numeric effect')
    for key,kind in (('restart','event'),('stop','end')):
        if key in entry and (choice or entry.get('kind')!=kind): raise ValueError('Lifecycle effect on wrong node type')

def model(path, page=None):
    root = ET.fromstring(Path(path).read_bytes())
    if root.tag == 'mxGraphModel': return root
    diagrams = list(root.iter('diagram'))
    if page: diagrams = [d for d in diagrams if d.get('name') == page or d.get('id') == page]
    if len(diagrams) != 1: raise ValueError('Select one diagram page with --page NAME')
    diagram = diagrams[0]
    child = diagram.find('mxGraphModel')
    if child is not None: return child
    encoded = (diagram.text or '').strip()
    if encoded.startswith('<'): return ET.fromstring(encoded)
    try:
        decoded = zlib.decompress(base64.b64decode(encoded), -15).decode('utf-8')
        return ET.fromstring(urllib.parse.unquote(decoded))
    except Exception as e: raise ValueError('Cannot decode diagram; export uncompressed XML from draw.io') from e

def convert(path, conversation_id, start=None, speaker='engineer', page=None, contact=None, request=None, trigger='request_arrived', cancel_when_resolved=False, contact_name=None, review_node=None):
    graph = model(path,page); root = graph.find('root')
    if root is None: raise ValueError('No mxGraphModel/root')
    nodes, edges, mapping = [], [], {}
    edge_ids = set()
    for element in root:
        cell = element if element.tag=='mxCell' else element.find('mxCell')
        if cell is not None and cell.get('edge')=='1': edge_ids.add(element.get('id') or cell.get('id'))
    edge_labels = {}
    for cell in root.iter('mxCell'):
        if cell.get('vertex')=='1' and cell.get('parent') in edge_ids:
            edge_labels.setdefault(cell.get('parent'),[]).append(plain(cell.get('value','')))

    for element in root:
        cell = element if element.tag == 'mxCell' else element.find('mxCell')
        if cell is None: continue
        attrs = {**cell.attrib, **element.attrib}
        cell_id = attrs.get('id')
        if not cell_id: continue
        label = attrs.get('text', attrs.get('label', attrs.get('value', '')))
        if cell.get('edge') == '1': edges.append((cell_id, cell.attrib, attrs, plain(label) or '\n'.join(edge_labels.get(cell_id,[])))); continue
        if cell.get('vertex') != '1' or cell.get('parent') in edge_ids: continue
        node_id = attrs.get('node_id',cell_id)
        if node_id in mapping.values(): raise ValueError('Duplicate node_id: '+node_id)
        mapping[cell_id] = node_id
        node = {'id':node_id, 'kind':attrs.get('kind','message'), 'speaker':attrs.get('speaker',speaker), 'text':plain(label), **properties(attrs)}
        nodes.append(node)
    by_id = {n['id']:n for n in nodes}
    for edge_id, cell, attrs, label in edges:
        if cell.get('source') not in mapping or cell.get('target') not in mapping: raise ValueError('Dangling edge: '+edge_id)
        source, target = mapping[cell['source']], mapping[cell['target']]
        kind = attrs.get('kind', 'choice' if label else 'next')
        if kind == 'choice':
            if not label: raise ValueError('Choice arrow needs text: '+edge_id)
            choice = {'id':attrs.get('choice_id',edge_id), 'text':label, 'next':target, **properties(attrs)}
            by_id[source].setdefault('choices',[]).append(choice)
        elif kind in ('next','true','false'):
            if properties(attrs): raise ValueError('Effects belong on boxes or choice arrows, not automatic links')
            key = {'next':'next','true':'on_true','false':'on_false'}[kind]
            if key in by_id[source]: raise ValueError('Duplicate '+key+' arrow from '+source)
            by_id[source][key] = target
        else: raise ValueError('Unknown arrow kind: '+kind)
    if not nodes: raise ValueError('No conversation nodes')
    start = start or nodes[0]['id']
    if start not in by_id: raise ValueError('Start does not name a node_id')
    capabilities = {'chat','company_chat','files','network','people','ai','news','activity','processes'}
    workday = contact is not None
    globals_metadata=dict(graph.attrib)
    variables = json.loads(graph.get('variables','{}'))
    validate_variables(variables)
    if workday and variables: raise ValueError("Numeric variables currently belong to opening graphs")
    checks = {'source_confirmed','task_readable','task_resolved','access_pending','access_decided','access_granted','access_denied','investigated','question_asked','promise_broken','delegated','delegation_pending','delegation_denied','mark_guidance','review_positive','review_negative','approval_decided','approval_accepted'}
    actions = {'investigate','ask_question','request_access','report_blocked','promise_delivery','confirm_source','share_file','request_delegation','decline_offer','ask_mark','draft_reply','send_chat_reply'}
    for n in nodes:
        if n['kind'] not in (('message','event','condition','end','wait') if workday else ('message','event','condition','end','wait')): raise ValueError('Unknown node kind: '+n['kind'])
        if n['kind']=='condition' and (not all(k in n for k in ('on_true','on_false')) or (not ('check' in n) if workday else not ('requires_flag' in n or 'test' in n))): raise ValueError('Condition needs a test and true/false arrows')
        if n['kind'] not in ('end','condition','wait') and not n.get('choices') and 'next' not in n: raise ValueError('Node needs a continuation or kind=end: '+n['id'])
        if not workday and n['kind']=='wait' and any(k in n for k in ('next','choices','reveal_interface', 'show_draft', 'record_instruction', 'show_task_document', 'important')): raise ValueError('Opening wait must hold without continuation or interface reveal')
        if 'choices' in n and 'next' in n: raise ValueError('Choose either choices or next: '+n['id'])
        seen = set()
        for entry in [n]+n.get('choices',[]):
            if any(c not in capabilities for c in entry.get('unlock',[])): raise ValueError('Unknown capability')
            if 'reveal_contacts' in entry and entry is n: raise ValueError('Contact reveals belong on player choices')
            if 'reveal_tabs' in entry:
                if entry is n: raise ValueError('Tab reveals belong on player choices')
                if any(t not in capabilities-{'company_chat'} for t in entry['reveal_tabs']): raise ValueError('Unknown tab')
            if workday and any(k in entry for k in ('test','restart','stop','reveal_interface')): raise ValueError('Lifecycle/condition nodes belong to opening graphs')
            validate_rules(entry,variables,entry is not n)
            if workday:
                if 'unlock' in entry or 'declaration' in entry: raise ValueError('Workday graphs use validated actions and flags instead of opening effects')
                for key in ('requires','check'):
                    if key in entry and entry[key] not in checks: raise ValueError('Unknown task condition')
                if 'action' in entry and (entry is n or entry['action'] not in actions): raise ValueError('Actions must be known player-choice actions')
                if entry.get('action')=='draft_reply' and (not isinstance(entry.get('reply'),str) or not entry['reply'] or not finite(entry.get('review_trust_delta'))): raise ValueError('Draft needs text and finite delayed trust effect')
                if 'document_index' in entry and (not isinstance(entry['document_index'],int) or entry['document_index'] < 0): raise ValueError('Document index must be a nonnegative integer')
                if entry.get('action') == 'promise_delivery' and not 0 < entry.get('within_seconds',0) <= 28800: raise ValueError('Promise needs positive within_seconds up to 28800')
            elif any(key in entry for key in ('action','requires','check','check_value','within_seconds','after_seconds')):
                raise ValueError('Task effects need --contact and --request')
            if entry is not n:
                if entry['id'] in seen: raise ValueError('Duplicate choice ID')
                seen.add(entry['id'])
        if workday and n['kind']=='wait':
            if 'next' not in n: raise ValueError('Workday wait needs a continuation')
            if ('check' in n) == ('after_seconds' in n): raise ValueError('Wait needs exactly one check or after_seconds')
            if 'after_seconds' in n and not n['after_seconds'] > 0: raise ValueError('Wait delay must be positive')
    if workday:
        if not request or trigger not in ('request_arrived','verified_delivery','scheduled_chat','reply_reviewed'): raise ValueError('Workday graph needs request ID and supported trigger')
        graph={'id':conversation_id,'contact':contact,'request_id':request,'trigger':trigger,'start':start,'cancel_when_resolved':cancel_when_resolved,'nodes':nodes}
        if trigger=='scheduled_chat':
            graph['arrive_after_seconds']=float(globals_metadata.get('arrive_after_seconds','0'))
            if not finite(graph['arrive_after_seconds']) or graph['arrive_after_seconds']<0: raise ValueError('Invalid chat arrival delay')
            if 'claim' in globals_metadata: graph['claim']=json.loads(globals_metadata['claim'])
        for key in ('approval_wait_node','source_conversation'):
            if key in globals_metadata: graph[key]=globals_metadata[key]
        if trigger=='reply_reviewed' and not graph.get('source_conversation'): raise ValueError('Review follow-up needs source conversation')
        approval=graph.get('approval_wait_node')
        if approval and (approval not in by_id or by_id[approval]['kind']!='wait'): raise ValueError('Approval continuation needs a wait node')
        if contact_name: graph['contact_name']=contact_name
        if review_node:
            if review_node not in by_id or by_id[review_node]['kind']!='wait': raise ValueError('Review continuation needs a wait node')
            graph['review_node']=review_node
        return {'schema_version':1,'conversations':[graph]}
    header={'id':conversation_id,'start':start}
    if variables: header['variables']=variables
    return {'schema_version':1, 'conversation':header, 'nodes':nodes}

def value(v):
    if isinstance(v,bool): return 'true' if v else 'false'
    if isinstance(v,dict): return '{ '+', '.join(json.dumps(k)+' = '+value(n) for k,n in v.items())+' }'
    if isinstance(v,list): return '[ '+', '.join(value(n) for n in v)+' ]'
    return json.dumps(v,ensure_ascii=False,allow_nan=False)

def toml(document):
    lines=['schema_version = 1']
    if 'rules' in document:
        lines+=['','[rules]']+[f'{key} = {value(v)}' for key,v in document['rules'].items()]
    workday='conversations' in document
    graphs=document['conversations'] if workday else [document]
    for graph in graphs:
        header=graph if workday else graph['conversation']
        lines+=['','[[conversations]]' if workday else '[conversation]']
        lines += [f'{key} = {value(v)}' for key,v in header.items() if key not in ('nodes','variables')]
        for name,spec in header.get('variables',{}).items():
            lines += ['', '[conversation.variables.'+json.dumps(name)+']']
            lines += [f'{key} = {value(v)}' for key,v in spec.items()]
        prefix='conversations.' if workday else ''
        for node in graph['nodes']:
            lines += ['',f'[[{prefix}nodes]]']
            lines += [f'{key} = {value(v)}' for key,v in node.items() if key!='choices']
            for choice in node.get('choices',[]):
                lines += [f'[[{prefix}nodes.choices]]']+[f'{key} = {value(v)}' for key,v in choice.items()]
    return '\n'.join(lines)+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input');parser.add_argument('output');parser.add_argument('--id',default='engineer-opening')
    parser.add_argument('--start');parser.add_argument('--speaker');parser.add_argument('--page')
    parser.add_argument('--contact');parser.add_argument('--contact-name');parser.add_argument('--review-node');parser.add_argument('--request');parser.add_argument('--trigger',default='request_arrived')
    parser.add_argument('--cancel-when-resolved',action='store_true')
    parser.add_argument('--append',action='store_true',help='Append a new graph to an existing workday catalog')
    args=parser.parse_args()
    try:
        result=convert(args.input,args.id,args.start,args.speaker or args.contact or 'engineer',args.page,args.contact,args.request,args.trigger,args.cancel_when_resolved,args.contact_name,args.review_node)
        if args.append:
            if not args.contact: raise ValueError('--append needs a workday graph')
            import tomllib
            old=tomllib.loads(Path(args.output).read_text(encoding='utf-8'))
            if old.get('schema_version')!=1 or not isinstance(old.get('conversations'),list): raise ValueError('Append target must be a workday catalog')
            if any(c['id']==args.id for c in old['conversations']): raise ValueError('Duplicate conversation ID; choose another ID or edit the existing graph')
            old['conversations']+=result['conversations'];result=old
        output=Path(args.output);output.parent.mkdir(parents=True,exist_ok=True);output.write_text(toml(result),encoding='utf-8')
    except (ValueError,ET.ParseError,OSError,KeyError) as e: parser.exit(1,str(e)+'\n')
    print(f'Converted graph to {args.output}')

if __name__=='__main__':main()
