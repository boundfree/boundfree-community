"""Source-independent exports retain units and sparse time coordinates."""
import numpy as np
import pytest
from boundfree_sidecar.writers.export import export_samples
from boundfree_sidecar.readers.registry import reader_for_path


def test_count_hdf5_roundtrip_preserves_gap_and_units(tmp_path):
    data=np.array([[10,20,np.nan,50,60],[100,200,np.nan,500,600]],dtype=np.float32)
    p=tmp_path/'selection.h5'
    export_samples(data,250,['A','B'],5,5.032,p,[],unit='count',channel_units=['count','count'],sample_times=[5,5.004,5.024,5.024,5.028],provenance={'processed':True,'source_id':'synthetic'})
    r=reader_for_path(p);m=r.metadata();s=r.get_slice(.024,.032)
    assert m.unit=='count' and m.channel_units==['count','count']
    assert m.duration==pytest.approx(.032)
    assert np.isnan(s.data[0][0])
    assert s.data[0][1:]==[50,60]
    assert s.sample_times==pytest.approx([.024,.024,.028])


def test_regular_count_csv_roundtrip_retains_unit_sidecar(tmp_path):
    p=tmp_path/'count.csv'
    export_samples([[10,20,30]],250,['A'],0,.012,p,[],unit='count')
    r=reader_for_path(p);assert r.metadata().unit=='count'
    assert r.get_slice(0,.012).data==[[10,20,30]]


@pytest.mark.parametrize('suffix',['csv','edf'])
def test_gap_export_requires_format_that_preserves_sparse_time(tmp_path,suffix):
    with pytest.raises(ValueError,match='contains gaps'):
        export_samples([[1,np.nan,2]],250,['A'],0,.02,tmp_path/f'gap.{suffix}',[],unit='count',sample_times=[0,.016,.016])


def test_count_snapshot_enters_float64_separation_and_bundle_without_false_uv(tmp_path):
    import json
    import h5py
    from boundfree_sidecar.protocol import handle_request
    def req(method,params,payload=None):
        r=handle_request({'id':method,'method':method,'params':params},payload)
        assert r['ok'],r
        return r['result']
    source=tmp_path/'snapshot.h5'
    original=np.array([10+np.arange(100),20-np.arange(100)],dtype=np.float64)
    with h5py.File(source,'w') as f:
        eeg=f.create_dataset('eeg',data=original)
        eeg.attrs.update(fs=250,unit='count',ch_names=['A','B'])
    meta=req('analysis_source_open',{'path':str(source)})
    assert meta['unit']=='count'
    read=req('analysis_source_read',{'handle':meta['handle'],'first_sample':0,'n_samples':100,'channel_ids':['0','1']})
    assert read['unit']=='count' and np.array_equal(read['data'],original)
    data=np.zeros((7,2,100),dtype='<f8');data[0]=data[1]=original
    mean=original.mean(axis=1);data[2]=original-mean[:,None]
    path=tmp_path/'result'
    req('analysis_bundle_write',{'path':str(path),'fs':250,'t0':0,'ch_names':['A','B'],'shape':[7,2,100],'dtype':'float64-le','unit':'count',
        'manifest':{'source':{'path':str(source),'fingerprint':meta['fingerprint'],'unit':'count'},'mean':mean.tolist()}},data.tobytes())
    result=json.loads((path/'analysis.json').read_text())
    assert result['unit']=='count' and result['error_unit']=='count'
    assert (path/'original_count.csv').is_file() and not (path/'original_uV.csv').exists()
