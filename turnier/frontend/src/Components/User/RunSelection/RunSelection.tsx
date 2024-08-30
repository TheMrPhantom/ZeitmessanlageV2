import { Button, Divider, Stack, ToggleButton, ToggleButtonGroup, Typography } from '@mui/material'
import React, { useState } from 'react'
import style from './runselection.module.scss'
import CalendarMonthIcon from '@mui/icons-material/CalendarMonth';
import PetsIcon from '@mui/icons-material/Pets';
import Spacer from '../../Common/Spacer';
import { Run, Size, SkillLevel } from '../../../types/ResponseTypes';
import { useNavigate } from 'react-router-dom';
import Dog from './Dog';
type Props = {}

const RunSelection = (props: Props) => {
    const [skillLevel, setskillLevel] = useState(SkillLevel.A3)
    const [jumpHeight, setjumpHeight] = useState(Size.Small)
    const navigate = useNavigate()


    return (
        <Stack
            direction="column"
            className={style.container}
            gap={2}
        >
            <Spacer vertical={3} />
            <Stack direction="column" gap={2}>

                <Stack gap={1}>
                    <Typography variant="overline">Veranstaltungs Infos</Typography>
                    <Stack direction="row" gap={1}>
                        <PetsIcon />
                        <Typography variant="h6">Neue Hundesporthalle e.V.</Typography>
                    </Stack>
                    <Stack direction="row" gap={1}>
                        <CalendarMonthIcon />
                        <Typography variant="h6">23.03.2025</Typography>
                    </Stack>
                </Stack>
                <Stack gap={1}>
                    <Typography variant="overline">Deine Hunde</Typography>
                    {/*<Typography variant="caption">Noch keine Hunde favorisiert</Typography>*/}
                    <Dog dogname="Fluffy McFluff"
                        resultA={{ time: 37.58, faults: 1, refusals: 0, class: Run.A0 }}
                        resultJ={{ time: -2, faults: 0, refusals: 0, class: Run.A0 }}
                        dogsLeft={null} />
                    <Dog dogname="Speedy Super Fast"
                        resultA={{ time: 37.58, faults: 1, refusals: 0, class: Run.A0 }}
                        resultJ={{ time: -2, faults: 0, refusals: 0, class: Run.A0 }}
                        dogsLeft={5} />
                </Stack>

            </Stack>
            <Divider />
            <Stack direction="column" gap={1}>
                <Typography variant="overline">Lauf auswählen</Typography>
                <Stack gap={1}>
                    <ToggleButtonGroup
                        exclusive
                        value={skillLevel}
                        fullWidth
                        onChange={(e, value) => { if (value != null) { setskillLevel(value) } }}
                    >
                        <ToggleButton value={SkillLevel.A0}>
                            <Typography variant="overline">A0</Typography>
                        </ToggleButton>
                        <ToggleButton value={SkillLevel.A1} >
                            <Typography variant="overline">A1</Typography>
                        </ToggleButton>
                        <ToggleButton value={SkillLevel.A2} >
                            <Typography variant="overline">A2</Typography>
                        </ToggleButton>
                        <ToggleButton value={SkillLevel.A3} >
                            <Typography variant="overline">A3</Typography>
                        </ToggleButton>
                    </ToggleButtonGroup>
                    <ToggleButtonGroup
                        exclusive
                        value={jumpHeight}
                        onChange={(e, value) => { if (value != null) { setjumpHeight(value) } }}
                        fullWidth
                    >
                        <ToggleButton value={Size.Small}>
                            <Typography variant="overline">Small</Typography>
                        </ToggleButton>
                        <ToggleButton value={Size.Medium}>
                            <Typography variant="overline">Medium</Typography>
                        </ToggleButton>
                        <ToggleButton value={Size.Intermediate} >
                            <Typography variant="overline">Intermediate</Typography>
                        </ToggleButton>
                        <ToggleButton value={Size.Large} >
                            <Typography variant="overline">Large</Typography>
                        </ToggleButton>
                    </ToggleButtonGroup>
                    <Spacer vertical={5} />
                    <Stack direction="row" flexWrap="wrap" gap={3} justifyContent="space-evenly">
                        <Button variant='contained'
                            className={style.runButton}
                            onClick={() => {
                                navigate(`${skillLevel * 2}/${jumpHeight}`)
                            }}
                        >
                            Zum A Lauf
                        </Button>
                        <Button variant='contained'
                            className={style.runButton}
                            onClick={() => {
                                navigate(`${skillLevel * 2 + 1}/${jumpHeight}`)
                            }}
                        >
                            Zum Jumping
                        </Button>
                    </Stack>
                </Stack>


            </Stack>
        </Stack>

    )
}

export default RunSelection