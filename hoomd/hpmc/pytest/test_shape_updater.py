import hoomd
import hoomd.hpmc
import numpy as np
import pytest

def test_before_attaching():
    verts = np.asarray([[-1, -1, -1], [-1, -1, 1], [-1, 1, -1], [1, -1, -1],
                        [-1, 1, 1], [1, -1, 1], [1, 1, -1], [1, 1, 1]])
    constant_move = hoomd.hpmc.shape_move.Constant(shape_params={'A': dict(vertices=verts,
                                                                           ignore_statistics=0,
                                                                           sweep_radius=0.0)})
    move_ratio = 1.0
    trigger = hoomd.trigger.Periodic(1)
    nselect = 1
    shape_updater = hoomd.hpmc.update.Shape(shape_move=constant_move,
                                            move_ratio=move_ratio,
                                            trigger=trigger,
                                            nselect=nselect)
    assert shape_updater.shape_move is constant_move
    assert np.allclose(shape_updater.move_ratio, move_ratio, rtol=1e-4)
    assert shape_updater.trigger is trigger
    assert shape_updater.nselect == nselect

    move_ratio = 0.5
    trigger = hoomd.trigger.Periodic(10)
    nselect = 2
    shape_updater.move_ratio = move_ratio
    shape_updater.trigger = trigger
    shape_updater.nselect = nselect
    assert np.allclose(shape_updater.move_ratio, move_ratio, rtol=1e-4)
    assert shape_updater.trigger is trigger
    assert shape_updater.nselect == nselect