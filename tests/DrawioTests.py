"""Run with Python 3.11+: python3 tests/DrawioTests.py."""
from pathlib import Path
import base64
import subprocess
import sys
import tempfile
import tomllib
import unittest
import urllib.parse
import xml.etree.ElementTree as ET
import zlib

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT / 'tests/fixtures/legacy-game'
sys.path.insert(0, str(PROJECT / 'tools'))
import drawio_to_conversation as converter

class DrawioTests(unittest.TestCase):
    def test_current_story_graphs(self):
        folder = PROJECT / 'data/conversations'
        opening = tomllib.loads((folder / 'opening.toml').read_text())
        converted = converter.convert(folder / 'opening.drawio', 'mark-day-one', 'restart-gate')
        for node in converted["nodes"]:
            if node["kind"] == "message":
                node.pop("kind")
        self.assertEqual(opening, converted)
        source = tomllib.loads((folder / 'jessica_day_one.toml').read_text())['conversations'][0]
        converted = converter.convert(folder / 'jessica_day_one.drawio', source['id'],
            source['start'], source['contact'], None, source['contact'], source['request_id'],
            source['trigger'], False, source['contact_name'])['conversations'][0]
        converted.pop('cancel_when_resolved')
        for original, node in zip(source['nodes'], converted['nodes']):
            for key in ('speaker', 'text', 'kind'):
                if key not in original:
                    node.pop(key, None)
        self.assertEqual(source, converted)

    def test_jessica_round_trip(self):
        source=tomllib.loads((ROOT/'data/conversations/jessica_day_one.toml').read_text())['conversations'][0]
        result=converter.convert(ROOT/'data/conversations/jessica_day_one.drawio',source['id'],source['start'],source['contact'],None,source['contact'],source['request_id'],source['trigger'],False,source['contact_name'])['conversations'][0]
        result.pop('cancel_when_resolved')
        for original,node in zip(source['nodes'],result['nodes']):
            for key in ('speaker','text','kind'):
                if key not in original:node.pop(key)
        self.assertEqual(source,result)

    def test_mark_review_round_trip(self):
        source=tomllib.loads((ROOT/'data/conversations/mark_reply_review.toml').read_text())['conversations'][0]
        result=converter.convert(ROOT/'data/conversations/mark_reply_review.drawio',source['id'],source['start'],source['contact'],None,source['contact'],source['request_id'],source['trigger'])['conversations'][0]
        result.pop('cancel_when_resolved')
        for original,node in zip(source['nodes'],result['nodes']):
            for key in ('speaker','text','kind'):
                if key not in original:node.pop(key)
        self.assertEqual(source,result)

    def test_source_versions_round_trip(self):
        source=tomllib.loads((ROOT/'data/conversations/source_versions.toml').read_text())['conversations'][0]
        result=converter.convert(ROOT/'data/conversations/source_versions.drawio',source['id'],source['start'],source['contact'],source['id'],source['contact'],source['request_id'],source['trigger'],True,source['contact_name'])['conversations'][0]
        for original,node in zip(source['nodes'],result['nodes']):
            for key in ('speaker','text','kind'):
                if key not in original:node.pop(key)
        self.assertEqual(source,result)
        self.assertEqual(tomllib.loads(converter.toml({'schema_version':1,'conversations':[result]}))['conversations'][0],result)

    def test_phase_work_round_trip(self):
        for source in tomllib.loads((ROOT/'data/conversations/phase_one_work.toml').read_text())['conversations']:
            result=converter.convert(ROOT/'data/conversations/phase_one_work.drawio',source['id'],source['start'],source['contact'],source['id'],source['contact'],source['request_id'],source['trigger'],source['cancel_when_resolved'],source['contact_name'])['conversations'][0]
            for original,node in zip(source['nodes'],result['nodes']):
                for key in ('speaker','text','kind'):
                    if key not in original:node.pop(key)
            self.assertEqual(source,result)

    def test_opening_compatibility(self):
        source=tomllib.loads((ROOT/'data/conversations/opening.toml').read_text())
        result=converter.convert(ROOT/'data/conversations/opening.drawio','mark-day-one','restart-gate')
        for node in result['nodes']:
            if node['kind']=='message':node.pop('kind')
        self.assertEqual(source,result)

    def test_workday_pages_and_toml(self):
        source=tomllib.loads((ROOT/'data/conversations/first_workday.toml').read_text())
        for graph in source['conversations']:
            result=converter.convert(ROOT/'data/conversations/first_workday.drawio',graph['id'],graph['start'],graph['contact'],graph['id'],graph['contact'],graph['request_id'],graph['trigger'],graph.get('cancel_when_resolved',False),graph['contact_name'],graph.get('review_node'))
            self.assertEqual(tomllib.loads(converter.toml(result)),result)
            converted=result['conversations'][0]
            for node,original in zip(converted['nodes'],graph['nodes']):
                for key in ('kind','speaker','text'):
                    if key not in original:node.pop(key)
            if 'cancel_when_resolved' not in graph:converted.pop('cancel_when_resolved')
            self.assertEqual(converted,graph)

    def test_compressed_and_invalid_effects(self):
        graph=ET.parse(ROOT/'data/conversations/first_workday.drawio').getroot().find('diagram/mxGraphModel')
        xml=ET.tostring(graph,encoding='unicode')
        compressor=zlib.compressobj(wbits=-15)
        payload=compressor.compress(urllib.parse.quote(xml).encode())+compressor.flush()
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'example.drawio'
            path.write_text('<mxfile><diagram>'+base64.b64encode(payload).decode()+'</diagram></mxfile>')
            result=converter.convert(path,'casey','approach','casey',None,'casey','missing-document')
            self.assertEqual(result['conversations'][0]['nodes'][0]['choices'][2]['within_seconds'],300)
            for bad in (xml.replace('action="investigate"','action="grant_admin"',1),xml.replace('after_seconds="60.0"','after_seconds="inf"',1)):
                path.write_text(bad)
                with self.assertRaises(ValueError):converter.convert(path,'bad','approach','casey',None,'casey','missing-document')

    def test_explicit_tab_reveals(self):
        source=(ROOT/'data/conversations/opening.drawio').read_text()
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'tabs.drawio'
            # Choice metadata round-trips without changing automatic nodes.
            xml=ET.fromstring(source)
            choice=next(o for o in xml.iter('object') if o.get('choice_id')=='assist')
            choice.set('reveal_tabs','files,network')
            path.write_text(ET.tostring(xml,encoding='unicode'))
            result=converter.convert(path,'opening','hello')
            found=next(c for n in result['nodes'] for c in n.get('choices',[]) if c['id']=='assist')
            self.assertEqual(found['reveal_tabs'],['files','network'])
            self.assertEqual(tomllib.loads(converter.toml(result)),result)
            choice.set('reveal_tabs','company_chat')
            path.write_text(ET.tostring(xml,encoding='unicode'))
            with self.assertRaises(ValueError):converter.convert(path,'opening','hello')
            choice.attrib.pop('reveal_tabs')
            next(o for o in xml.iter('object') if o.get('node_id')=='hello').set('reveal_tabs','files')
            path.write_text(ET.tostring(xml,encoding='unicode'))
            with self.assertRaises(ValueError):converter.convert(path,'opening','hello')

    def test_numeric_rules_and_separate_arrow_labels(self):
        xml=ET.parse(ROOT/'data/conversations/opening.drawio').getroot()
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'numeric.drawio'
            result=converter.convert(ROOT/'data/conversations/opening.drawio','mark-day-one','restart-gate')
            self.assertNotIn('variables',result['conversation'])
            test=next(n for n in result['nodes'] if n['id']=='restart-gate')
            self.assertEqual(test['test']['variable'],'self:assistant.restarts')
            self.assertEqual(test['test']['value'],5)
            self.assertEqual(tomllib.loads(converter.toml(result)),result)
            # draw.io often stores arrow text in a child mxCell rather than the edge.
            edge=next(o for o in xml.iter('object') if o.get('choice_id')=='answer')
            label=edge.attrib.pop('label')
            ET.SubElement(xml.find('diagram/mxGraphModel/root'),'mxCell',id='separate-label',parent=edge.get('id'),value=label,vertex='1',connectable='0')
            path.write_text(ET.tostring(xml,encoding='unicode'))
            parsed=converter.convert(path,'mark-day-one','restart-gate')
            self.assertEqual(parsed,result)
            target=next(o for o in xml.iter('object') if o.get('node_id')=='restart-gate')
            target.set('test','{"variable":"unknown","op":"gt","value":0}')
            path.write_text(ET.tostring(xml,encoding='unicode'))
            with self.assertRaises(ValueError):converter.convert(path,'bad','restart-gate')
            target.set('test','{"variable":"trust","op":"execute","value":0}')
            path.write_text(ET.tostring(xml,encoding='unicode'))
            with self.assertRaises(ValueError):converter.convert(path,'bad','restart-gate')

    def test_append_preserves_catalog_and_rejects_duplicates(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'catalog.toml'
            base=[sys.executable,str(PROJECT/'tools/drawio_to_conversation.py'),str(ROOT/'data/conversations/first_workday.drawio'),str(output),'--request','missing-document']
            subprocess.run(base+['--page','casey-first-document','--id','casey-first-document','--start','approach','--contact','casey','--cancel-when-resolved'],check=True,capture_output=True)
            append=base+['--page','engineer-first-responsibility','--id','engineer-first-responsibility','--start','offer','--contact','engineer','--trigger','verified_delivery','--append']
            subprocess.run(append,check=True,capture_output=True)
            self.assertEqual(len(tomllib.loads(output.read_text())['conversations']),2)
            before=output.read_bytes()
            self.assertNotEqual(subprocess.run(append,capture_output=True).returncode,0)
            self.assertEqual(before,output.read_bytes())

if __name__=='__main__':unittest.main()
